#include "FileOperationEngine.h"
#include "XdgTrash.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <sys/stat.h>
#include <utime.h>

FileOperationEngine::FileOperationEngine(QObject* parent)
    : QObject(parent)
{
}

FileOperationEngine::~FileOperationEngine()
{
    cancel();
}

void FileOperationEngine::cancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

bool FileOperationEngine::isRunning() const
{
    return m_running.load(std::memory_order_relaxed);
}

QString FileOperationEngine::generateDuplicateName(const QString& path)
{
    QFileInfo fi(path);
    QString dir = fi.absolutePath();
    QString base = fi.completeBaseName();
    QString suffix = fi.suffix();

    QString candidate;
    int count = 1;
    do {
        QString copySuffix = (count == 1) ? QStringLiteral(" copy") : QStringLiteral(" copy %1").arg(count);
        if (!suffix.isEmpty()) {
            candidate = QStringLiteral("%1/%2%3.%4").arg(dir, base, copySuffix, suffix);
        } else {
            candidate = QStringLiteral("%1/%2%3").arg(dir, base, copySuffix);
        }
        count++;
    } while (QFile::exists(candidate));

    return candidate;
}

QString FileOperationEngine::generateKeepBothName(const QString& destinationPath)
{
    return generateDuplicateName(destinationPath);
}

void FileOperationEngine::copyItems(const QStringList& sources, const QString& destinationDir, ConflictResolution res)
{
    runAsyncOperation(OperationType::Copy, sources, destinationDir, res);
}

void FileOperationEngine::moveItems(const QStringList& sources, const QString& destinationDir, ConflictResolution res)
{
    runAsyncOperation(OperationType::Move, sources, destinationDir, res);
}

void FileOperationEngine::duplicateItems(const QStringList& sources)
{
    runAsyncOperation(OperationType::Duplicate, sources, QString(), ConflictResolution::KeepBoth);
}

void FileOperationEngine::trashItems(const QStringList& sources)
{
    runAsyncOperation(OperationType::Trash, sources, QString(), ConflictResolution::Skip);
}

void FileOperationEngine::deletePermanently(const QStringList& sources)
{
    runAsyncOperation(OperationType::Delete, sources, QString(), ConflictResolution::Skip);
}

bool FileOperationEngine::createFolder(const QString& parentDir, const QString& folderName, QString* createdPath, QString* error)
{
    QDir dir(parentDir);
    QString newPath = dir.filePath(folderName);
    if (QFile::exists(newPath)) {
        if (error) *error = QStringLiteral("A folder or file with this name already exists.");
        return false;
    }
    if (!dir.mkdir(folderName)) {
        if (error) *error = QStringLiteral("Failed to create folder. Permission denied or invalid name.");
        return false;
    }
    if (createdPath) *createdPath = newPath;
    return true;
}

bool FileOperationEngine::createFile(const QString& parentDir, const QString& fileName, QString* createdPath, QString* error)
{
    QDir dir(parentDir);
    QString newPath = dir.filePath(fileName);
    if (QFile::exists(newPath)) {
        if (error) *error = QStringLiteral("A file with this name already exists.");
        return false;
    }
    QFile file(newPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.close();
    if (createdPath) *createdPath = newPath;
    return true;
}

bool FileOperationEngine::renameItem(const QString& sourcePath, const QString& newName, QString* renamedPath, QString* error)
{
    QFileInfo fi(sourcePath);
    if (!fi.exists()) {
        if (error) *error = QStringLiteral("Source item does not exist.");
        return false;
    }
    QString destPath = fi.dir().filePath(newName);
    if (QFile::exists(destPath)) {
        if (error) *error = QStringLiteral("Destination item already exists.");
        return false;
    }
    if (!QFile::rename(sourcePath, destPath)) {
        if (error) *error = QStringLiteral("Failed to rename item. Permission denied or file in use.");
        return false;
    }
    if (renamedPath) *renamedPath = destPath;
    return true;
}

bool FileOperationEngine::createSymlink(const QString& target, const QString& linkPath, QString* error)
{
    if (QFile::exists(linkPath)) {
        if (error) *error = QStringLiteral("Link path already exists.");
        return false;
    }
    if (!QFile::link(target, linkPath)) {
        if (error) *error = QStringLiteral("Failed to create symlink.");
        return false;
    }
    return true;
}

void FileOperationEngine::runAsyncOperation(OperationType type, const QStringList& sources, const QString& destDir, ConflictResolution defaultRes)
{
    if (m_running.load()) {
        return; // already running an operation
    }

    m_cancelled.store(false);
    m_running.store(true);

    QThread* thread = QThread::create([this, type, sources, destDir, defaultRes]() {
        switch (type) {
        case OperationType::Copy:
        case OperationType::Move:
            executeCopyOrMove(type, sources, destDir, defaultRes);
            break;
        case OperationType::Duplicate:
            executeDuplicate(sources);
            break;
        case OperationType::Trash:
            executeTrash(sources);
            break;
        case OperationType::Delete:
            executeDelete(sources);
            break;
        default:
            break;
        }
        m_running.store(false);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void FileOperationEngine::executeCopyOrMove(OperationType type, const QStringList& sources, const QString& destDir, ConflictResolution defaultRes)
{
    // Calculate total items and total size
    qint64 totalBytes = 0;
    int totalItems = 0;

    for (const auto& src : sources) {
        QFileInfo fi(src);
        if (fi.isDir()) {
            QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                totalItems++;
                totalBytes += it.fileInfo().size();
            }
        } else {
            totalItems++;
            totalBytes += fi.size();
        }
    }

    emit operationStarted(type, totalItems, totalBytes);

    m_currentProgress.type = type;
    m_currentProgress.totalBytes = totalBytes;
    m_currentProgress.totalItems = totalItems;
    m_currentProgress.bytesProcessed = 0;
    m_currentProgress.itemsProcessed = 0;

    ConflictResolution appliedResolution = defaultRes;
    bool applyToAll = (defaultRes != ConflictResolution::Ask);

    OperationResult result;
    QDir targetDir(destDir);

    QElapsedTimer timer;
    timer.start();

    for (const auto& src : sources) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            result.success = false;
            result.errorMessage = QStringLiteral("Operation cancelled by user.");
            break;
        }

        QFileInfo srcInfo(src);
        QString destPath = targetDir.filePath(srcInfo.fileName());

        bool opSuccess = false;
        if (type == OperationType::Copy) {
            if (srcInfo.isDir()) {
                opSuccess = copyDirectoryRecursive(src, destPath, appliedResolution, applyToAll);
            } else {
                opSuccess = copySingleFile(src, destPath, appliedResolution, applyToAll);
            }
        } else { // Move
            opSuccess = moveSingleItem(src, destPath, appliedResolution, applyToAll);
        }

        if (opSuccess) {
            result.processedCount++;
            result.createdPaths.append(destPath);
        } else {
            result.errorCount++;
        }
    }

    emit operationFinished(result);
}

bool FileOperationEngine::copySingleFile(const QString& src, const QString& dst, ConflictResolution& appliedRes, bool& applyToAll)
{
    QString finalDst = dst;
    if (QFile::exists(finalDst)) {
        if (!applyToAll || appliedRes == ConflictResolution::Ask) {
            OperationConflict conflict;
            conflict.sourcePath = src;
            conflict.destinationPath = finalDst;
            QFileInfo srcFi(src);
            QFileInfo dstFi(finalDst);
            conflict.sourceSize = srcFi.size();
            conflict.destinationSize = dstFi.size();
            conflict.sourceModified = srcFi.lastModified();
            conflict.destinationModified = dstFi.lastModified();

            // Ask on main thread
            QMetaObject::invokeMethod(this, [this, conflict, &appliedRes, &applyToAll]() {
                emit conflictEncountered(conflict, appliedRes, applyToAll);
            }, Qt::BlockingQueuedConnection);
        }

        if (appliedRes == ConflictResolution::Cancel) {
            cancel();
            return false;
        } else if (appliedRes == ConflictResolution::Skip) {
            return true;
        } else if (appliedRes == ConflictResolution::KeepBoth) {
            finalDst = generateKeepBothName(finalDst);
        } else if (appliedRes == ConflictResolution::Replace) {
            QFile::remove(finalDst);
        }
    }

    // Perform safe chunked copy
    QFile srcFile(src);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        return false;
    }

    // Write to a temporary file in the destination directory first
    QFileInfo dstFi(finalDst);
    QString tempDst = dstFi.absolutePath() + QStringLiteral("/.finder_tmp_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QFile dstFile(tempDst);
    if (!dstFile.open(QIODevice::WriteOnly)) {
        return false;
    }

    constexpr qint64 ChunkSize = 64 * 1024; // 64 KB
    QByteArray buffer(ChunkSize, Qt::Uninitialized);

    while (!srcFile.atEnd()) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            dstFile.close();
            QFile::remove(tempDst);
            return false;
        }

        qint64 bytesRead = srcFile.read(buffer.data(), ChunkSize);
        if (bytesRead > 0) {
            dstFile.write(buffer.data(), bytesRead);
            m_currentProgress.bytesProcessed += bytesRead;

            m_currentProgress.percent = m_currentProgress.totalBytes > 0
                ? static_cast<int>((m_currentProgress.bytesProcessed * 100) / m_currentProgress.totalBytes)
                : 100;
            m_currentProgress.currentItem = dstFi.fileName();
            emit progress(m_currentProgress);
        }
    }

    dstFile.flush();
    dstFile.close();
    srcFile.close();

    // Preserve permissions
    dstFile.setPermissions(srcFile.permissions());

    // Rename temp into place
    if (QFile::exists(finalDst)) {
        QFile::remove(finalDst);
    }
    if (!QFile::rename(tempDst, finalDst)) {
        QFile::remove(tempDst);
        return false;
    }

    m_currentProgress.itemsProcessed++;
    return true;
}

bool FileOperationEngine::copyDirectoryRecursive(const QString& src, const QString& dst, ConflictResolution& appliedRes, bool& applyToAll)
{
    QDir srcDir(src);
    if (!QDir().mkpath(dst)) {
        return false;
    }

    for (const auto& entry : srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            return false;
        }

        QString srcItem = entry.absoluteFilePath();
        QString dstItem = dst + QStringLiteral("/") + entry.fileName();

        if (entry.isDir()) {
            if (!copyDirectoryRecursive(srcItem, dstItem, appliedRes, applyToAll)) {
                return false;
            }
        } else {
            if (!copySingleFile(srcItem, dstItem, appliedRes, applyToAll)) {
                return false;
            }
        }
    }

    return true;
}

bool FileOperationEngine::moveSingleItem(const QString& src, const QString& dst, ConflictResolution& appliedRes, bool& applyToAll)
{
    QString finalDst = dst;
    if (QFile::exists(finalDst)) {
        if (!applyToAll || appliedRes == ConflictResolution::Ask) {
            OperationConflict conflict;
            conflict.sourcePath = src;
            conflict.destinationPath = finalDst;
            QFileInfo srcFi(src);
            QFileInfo dstFi(finalDst);
            conflict.sourceSize = srcFi.size();
            conflict.destinationSize = dstFi.size();
            conflict.sourceModified = srcFi.lastModified();
            conflict.destinationModified = dstFi.lastModified();

            QMetaObject::invokeMethod(this, [this, conflict, &appliedRes, &applyToAll]() {
                emit conflictEncountered(conflict, appliedRes, applyToAll);
            }, Qt::BlockingQueuedConnection);
        }

        if (appliedRes == ConflictResolution::Cancel) {
            cancel();
            return false;
        } else if (appliedRes == ConflictResolution::Skip) {
            return true;
        } else if (appliedRes == ConflictResolution::KeepBoth) {
            finalDst = generateKeepBothName(finalDst);
        } else if (appliedRes == ConflictResolution::Replace) {
            QFileInfo dstInfo(finalDst);
            if (dstInfo.isDir()) {
                QDir(finalDst).removeRecursively();
            } else {
                QFile::remove(finalDst);
            }
        }
    }

    // Try atomic rename first
    if (QFile::rename(src, finalDst)) {
        m_currentProgress.itemsProcessed++;
        emit progress(m_currentProgress);
        return true;
    }

    // Cross-filesystem move fallback: copy safely, then delete source
    QFileInfo srcInfo(src);
    bool copySuccess = false;
    if (srcInfo.isDir()) {
        copySuccess = copyDirectoryRecursive(src, finalDst, appliedRes, applyToAll);
        if (copySuccess) {
            QDir(src).removeRecursively();
        }
    } else {
        copySuccess = copySingleFile(src, finalDst, appliedRes, applyToAll);
        if (copySuccess) {
            QFile::remove(src);
        }
    }

    return copySuccess;
}

void FileOperationEngine::executeDuplicate(const QStringList& sources)
{
    emit operationStarted(OperationType::Duplicate, static_cast<int>(sources.size()), 0);
    OperationResult result;
    ConflictResolution res = ConflictResolution::KeepBoth;
    bool applyAll = true;

    for (const auto& src : sources) {
        if (m_cancelled.load(std::memory_order_relaxed)) break;
        QString dst = generateDuplicateName(src);
        QFileInfo fi(src);
        bool ok = false;
        if (fi.isDir()) {
            ok = copyDirectoryRecursive(src, dst, res, applyAll);
        } else {
            ok = copySingleFile(src, dst, res, applyAll);
        }
        if (ok) {
            result.processedCount++;
            result.createdPaths.append(dst);
        } else {
            result.errorCount++;
        }
    }
    emit operationFinished(result);
}

void FileOperationEngine::executeTrash(const QStringList& sources)
{
    emit operationStarted(OperationType::Trash, static_cast<int>(sources.size()), 0);
    OperationResult result;
    for (const auto& src : sources) {
        if (m_cancelled.load(std::memory_order_relaxed)) break;
        QString error;
        if (XdgTrash::moveToTrash(src, &error)) {
            result.processedCount++;
        } else {
            result.errorCount++;
            result.errorMessage = error;
        }
    }
    emit operationFinished(result);
}

void FileOperationEngine::executeDelete(const QStringList& sources)
{
    emit operationStarted(OperationType::Delete, static_cast<int>(sources.size()), 0);
    OperationResult result;
    for (const auto& src : sources) {
        if (m_cancelled.load(std::memory_order_relaxed)) break;
        QFileInfo fi(src);
        bool ok = false;
        if (fi.isDir()) {
            ok = QDir(src).removeRecursively();
        } else {
            ok = QFile::remove(src);
        }
        if (ok) {
            result.processedCount++;
        } else {
            result.errorCount++;
        }
    }
    emit operationFinished(result);
}
