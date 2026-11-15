#include "FileOperationEngine.h"
#include "ArchiveService.h"
#include "XdgTrash.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QPointer>
#include <QRandomGenerator>
#include <QSet>
#include <QtConcurrent>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace {

QString absoluteCleanPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString& a, const QString& b) {
    return absoluteCleanPath(a) == absoluteCleanPath(b);
}

// Catches hard links (and bind-mount aliases) that path comparison misses.
bool sameInode(const QString& a, const QString& b) {
    struct stat sa{};
    struct stat sb{};
    const QByteArray pa = QFile::encodeName(a);
    const QByteArray pb = QFile::encodeName(b);
    if (::lstat(pa.constData(), &sa) != 0 || ::lstat(pb.constData(), &sb) != 0) {
        return false;
    }
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

QString uniqueSiblingPath(const QString& directory, const QString& prefix) {
    return directory + QLatin1Char('/') + prefix + QString::number(::getpid()) + QLatin1Char('_') +
           QString::number(QDateTime::currentMSecsSinceEpoch()) + QLatin1Char('_') +
           QString::number(QRandomGenerator::global()->generate());
}

// POSIX rename, unlike QFile::rename, atomically replaces an existing file.
bool posixRename(const QString& from, const QString& to) {
    const QByteArray fromBytes = QFile::encodeName(from);
    const QByteArray toBytes = QFile::encodeName(to);
    return ::rename(fromBytes.constData(), toBytes.constData()) == 0;
}

void syncDirectory(const QString& directory) {
    const QByteArray dirBytes = QFile::encodeName(directory);
    const int fd = ::open(dirBytes.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return;
    ::fsync(fd);
    ::close(fd);
}

// Moves a fully written temporary item into place. The previous destination is
// never removed before the replacement is ready, and it is restored if the
// final rename fails.
bool installTemporary(const QString& tempPath, const QString& finalPath, QString* error) {
    QFileInfo dstInfo(finalPath);
    const bool dstExists = dstInfo.exists() || dstInfo.isSymLink();

    if (!dstExists) {
        if (posixRename(tempPath, finalPath)) {
            syncDirectory(dstInfo.absolutePath());
            return true;
        }
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    if (dstInfo.isDir() && !dstInfo.isSymLink()) {
        const QString backupPath =
            uniqueSiblingPath(dstInfo.absolutePath(), QStringLiteral(".finder_old_"));
        if (!posixRename(finalPath, backupPath)) {
            if (error)
                *error = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        if (!posixRename(tempPath, finalPath)) {
            const QString failure = QString::fromLocal8Bit(std::strerror(errno));
            posixRename(backupPath, finalPath);
            if (error)
                *error = failure;
            return false;
        }
        QDir(backupPath).removeRecursively();
        syncDirectory(dstInfo.absolutePath());
        return true;
    }

    if (posixRename(tempPath, finalPath)) {
        syncDirectory(dstInfo.absolutePath());
        return true;
    }
    if (error)
        *error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
}

void copyExtendedAttributes(const QString& src, const QString& dst, bool symlink) {
    const QByteArray srcBytes = QFile::encodeName(src);
    const QByteArray dstBytes = QFile::encodeName(dst);

    const auto readNames = [&](char* buffer, size_t size) -> ssize_t {
        return symlink ? ::llistxattr(srcBytes.constData(), buffer, size)
                       : ::listxattr(srcBytes.constData(), buffer, size);
    };
    const auto readValue = [&](const char* name, char* buffer, size_t size) -> ssize_t {
        return symlink ? ::lgetxattr(srcBytes.constData(), name, buffer, size)
                       : ::getxattr(srcBytes.constData(), name, buffer, size);
    };
    const auto writeValue = [&](const char* name, const char* value, size_t size) {
        return symlink ? ::lsetxattr(dstBytes.constData(), name, value, size, 0)
                       : ::setxattr(dstBytes.constData(), name, value, size, 0);
    };

    QVector<QByteArray> names;
    const ssize_t listSize = readNames(nullptr, 0);
    if (listSize > 0) {
        QByteArray nameBuffer(static_cast<int>(listSize), '\0');
        const ssize_t readSize =
            readNames(nameBuffer.data(), static_cast<size_t>(nameBuffer.size()));
        if (readSize > 0) {
            qsizetype offset = 0;
            while (offset < readSize) {
                const char* name = nameBuffer.constData() + offset;
                const qsizetype length = static_cast<qsizetype>(std::strlen(name));
                if (length == 0) {
                    break;
                }
                names.append(QByteArray(name, static_cast<int>(length)));
                offset += length + 1;
            }
        }
    }

    // ACLs are xattrs too; try them explicitly in case listxattr() hides them.
    for (const char* aclName : {"system.posix_acl_access", "system.posix_acl_default"}) {
        if (!names.contains(QByteArray(aclName))) {
            names.append(QByteArray(aclName));
        }
    }

    for (const QByteArray& name : names) {
        const ssize_t valueSize = readValue(name.constData(), nullptr, 0);
        if (valueSize < 0) {
            continue;
        }

        QByteArray value(static_cast<int>(valueSize), '\0');
        if (valueSize > 0) {
            const ssize_t readSize =
                readValue(name.constData(), value.data(), static_cast<size_t>(value.size()));
            if (readSize < 0) {
                continue;
            }
            value.resize(static_cast<int>(readSize));
        }
        writeValue(name.constData(), value.constData(), static_cast<size_t>(value.size()));
    }
}

void copyMetadata(const QString& src, const QString& dst, const struct stat& st, bool symlink) {
    const QByteArray dstBytes = QFile::encodeName(dst);

    // Ownership first: chown() can clear setuid/setgid bits, so restore the
    // mode afterwards. Only root can change ownership; for normal users the
    // destination is already owned by the current user.
    if (::geteuid() == 0) {
        struct stat dstStat{};
        if (::lstat(dstBytes.constData(), &dstStat) == 0 &&
            (dstStat.st_uid != st.st_uid || dstStat.st_gid != st.st_gid)) {
            if (symlink) {
                ::lchown(dstBytes.constData(), st.st_uid, st.st_gid);
            } else {
                ::chown(dstBytes.constData(), st.st_uid, st.st_gid);
            }
        }
    }

    if (!symlink) {
        ::chmod(dstBytes.constData(), st.st_mode & 07777);
    }

    copyExtendedAttributes(src, dst, symlink);

    struct timespec times[2] = {st.st_atim, st.st_mtim};
    ::utimensat(AT_FDCWD, dstBytes.constData(), times, symlink ? AT_SYMLINK_NOFOLLOW : 0);
}

} // namespace

FileOperationEngine::FileOperationEngine(QObject* parent) : QObject(parent) {
    qRegisterMetaType<OperationItem>("OperationItem");
    qRegisterMetaType<QList<OperationItem>>("QList<OperationItem>");
}

FileOperationEngine::~FileOperationEngine() {
    cancel();
    if (m_workerThread) {
        // Give the worker a chance to unwind (it polls m_cancelled per chunk and
        // per entry) so no thread touches this object after destruction.
        if (!m_workerThread->wait(10000)) {
            m_workerThread->terminate();
            m_workerThread->wait(2000);
        }
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

void FileOperationEngine::cancel() {
    m_cancelled.store(true, std::memory_order_relaxed);
}

void FileOperationEngine::cancelAll() {
    cancel();
    m_pendingJobs.clear();
    m_currentJobIndex = 0;
    m_totalJobsInBatch = 0;
    emit queueStatusChanged(0, 0);
}

void FileOperationEngine::pause() {
    m_paused.store(true, std::memory_order_relaxed);
}

void FileOperationEngine::resume() {
    m_paused.store(false, std::memory_order_relaxed);
}

bool FileOperationEngine::isPaused() const {
    return m_paused.load(std::memory_order_relaxed);
}

void FileOperationEngine::skipCurrentFile() {
    m_skipCurrentFile.store(true, std::memory_order_relaxed);
}

void FileOperationEngine::cancelItem(quint64 itemId) {
    QMutexLocker locker(&m_itemMutex);
    auto it = m_itemCancelFlags.find(itemId);
    if (it != m_itemCancelFlags.end() && it.value()) {
        it.value()->store(true, std::memory_order_relaxed);
    }
}

QVector<quint64> FileOperationEngine::beginItems(const QStringList& sources, OperationType type) {
    QList<OperationItem> items;
    QVector<quint64> ids;
    items.reserve(sources.size());
    ids.reserve(sources.size());
    {
        QMutexLocker locker(&m_itemMutex);
        m_itemCancelFlags.clear();
        m_itemInfo.clear();
        m_currentItemIds.clear();
        for (const QString& source : sources) {
            const quint64 id = m_nextItemId++;
            OperationItem item;
            item.id = id;
            item.sourcePath = source;
            item.displayName = QFileInfo(source).fileName();
            if (item.displayName.isEmpty())
                item.displayName = source;
            item.totalBytes = QFileInfo(source).isDir() ? 0 : QFileInfo(source).size();
            items.append(item);
            ids.append(id);
            m_itemCancelFlags.insert(id, std::make_shared<std::atomic<bool>>(false));
            m_itemInfo.insert(id, item);
        }
        m_currentItemIds = ids;
        m_activeItemId.store(0, std::memory_order_relaxed);
    }
    emit operationItemsReset(items);
    Q_UNUSED(type);
    return ids;
}

void FileOperationEngine::setItemStatus(quint64 id, OperationItemStatus status,
                                        const QString& detail) {
    qint64 processed = 0;
    {
        QMutexLocker locker(&m_itemMutex);
        auto it = m_itemInfo.find(id);
        if (it == m_itemInfo.end())
            return;
        it->status = status;
        it->detail = detail;
        processed = it->processedBytes;
    }
    emit operationItemChanged(id, static_cast<int>(status), detail, processed);
}

bool FileOperationEngine::isItemCancelled(quint64 id) const {
    QMutexLocker locker(&m_itemMutex);
    auto it = m_itemCancelFlags.constFind(id);
    return it != m_itemCancelFlags.constEnd() && it.value() &&
           it.value()->load(std::memory_order_relaxed);
}

bool FileOperationEngine::activeItemCancelled() const {
    const quint64 id = m_activeItemId.load(std::memory_order_relaxed);
    return id != 0 && isItemCancelled(id);
}

void FileOperationEngine::waitIfPaused() {
    while (m_paused.load(std::memory_order_relaxed) &&
           !m_cancelled.load(std::memory_order_relaxed) && !activeItemCancelled()) {
        QThread::msleep(40);
    }
}

void FileOperationEngine::resetProgressTiming() {
    m_progressStartMs = QDateTime::currentMSecsSinceEpoch();
    m_lastProgressEmitMs = m_progressStartMs;
    m_lastTimeMs = m_progressStartMs;
    m_lastBytes = m_currentProgress.bytesProcessed;
    m_currentProgress.elapsedMs = 0;
    m_currentProgress.remainingMs = -1;
    m_currentProgress.speedBytesPerSec = 0.0;
}

void FileOperationEngine::updateProgressTiming() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_progressStartMs <= 0) {
        m_progressStartMs = now;
    }
    m_currentProgress.elapsedMs = now - m_progressStartMs;

    const qint64 timeDelta = now - m_lastTimeMs;
    if (timeDelta >= 200) {
        const qint64 bytesDelta = m_currentProgress.bytesProcessed - m_lastBytes;
        const double seconds = static_cast<double>(timeDelta) / 1000.0;
        if (seconds > 0.0) {
            m_currentProgress.speedBytesPerSec = static_cast<double>(bytesDelta) / seconds;
        }
        m_lastTimeMs = now;
        m_lastBytes = m_currentProgress.bytesProcessed;
    }

    const qint64 accountedBytes =
        m_currentProgress.bytesProcessed + m_currentProgress.bytesCancelled;
    if (m_currentProgress.speedBytesPerSec > 0.0 && m_currentProgress.totalBytes > accountedBytes) {
        const double remainingSeconds =
            static_cast<double>(m_currentProgress.totalBytes - accountedBytes) /
            m_currentProgress.speedBytesPerSec;
        m_currentProgress.remainingMs = static_cast<qint64>(remainingSeconds * 1000.0);
    } else if (m_currentProgress.totalBytes > 0 && accountedBytes >= m_currentProgress.totalBytes) {
        m_currentProgress.remainingMs = 0;
    }
}

bool FileOperationEngine::isRunning() const {
    return m_running.load(std::memory_order_relaxed);
}

int FileOperationEngine::pendingJobCount() const {
    return static_cast<int>(m_pendingJobs.size());
}

int FileOperationEngine::activeAndPendingJobCount() const {
    return (m_running.load(std::memory_order_relaxed) ? 1 : 0) +
           static_cast<int>(m_pendingJobs.size());
}

bool FileOperationEngine::startWorker(const std::function<void()>& job) {
    if (m_running.exchange(true)) {
        // Another operation is running: queue this one instead of dropping it.
        m_pendingJobs.enqueue(job);
        m_totalJobsInBatch++;
        emit queueStatusChanged(m_currentJobIndex, m_totalJobsInBatch);
        return true;
    }

    if (m_totalJobsInBatch == 0) {
        m_currentJobIndex = 1;
        m_totalJobsInBatch = 1;
    }
    emit queueStatusChanged(m_currentJobIndex, m_totalJobsInBatch);

    m_cancelled.store(false);
    m_paused.store(false);
    m_skipCurrentFile.store(false);
    m_currentProgress.itemsCancelled = 0;
    m_currentProgress.bytesCancelled = 0;
    m_folderResolution = ConflictResolution::Ask;
    m_folderApplyToAll = false;
    m_itemErrors.clear();
    m_itemWarnings.clear();
    m_skippedItems = 0;
    m_failOnSkip = false;

    m_workerThread = QThread::create([this, job]() {
        job();
        m_running.store(false);
        QMetaObject::invokeMethod(this, [this]() { startNextQueuedJob(); }, Qt::QueuedConnection);
    });

    QPointer<QThread> thread = m_workerThread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (thread && m_workerThread == thread.data()) {
            m_workerThread = nullptr;
        }
        if (thread) {
            thread->deleteLater();
        }
    });
    thread->start();
    return true;
}

void FileOperationEngine::startNextQueuedJob() {
    if (m_running.load(std::memory_order_relaxed) || m_pendingJobs.isEmpty()) {
        if (!m_running.load(std::memory_order_relaxed) && m_pendingJobs.isEmpty()) {
            m_currentJobIndex = 0;
            m_totalJobsInBatch = 0;
            emit queueStatusChanged(0, 0);
        }
        return;
    }
    m_currentJobIndex++;
    const std::function<void()> job = m_pendingJobs.dequeue();
    startWorker(job);
}

void FileOperationEngine::recordError(const QString& path, const QString& reason) {
    if (m_itemErrors.size() < 100) {
        m_itemErrors.append(QStringLiteral("%1 — %2").arg(path, reason));
    }
}

void FileOperationEngine::recordWarning(const QString& path, const QString& reason) {
    if (m_itemWarnings.size() < 100) {
        m_itemWarnings.append(QStringLiteral("%1 — %2").arg(path, reason));
    }
}

void FileOperationEngine::applyCollectedIssues(OperationResult& result) {
    result.skippedCount = m_skippedItems;
    if (!m_itemWarnings.isEmpty()) {
        result.warnings = m_itemWarnings;
    }
    if (m_itemErrors.isEmpty()) {
        return;
    }

    result.success = false;
    result.errorCount = qMax(result.errorCount, static_cast<int>(m_itemErrors.size()));
    const int shown = qMin(8, static_cast<int>(m_itemErrors.size()));
    QString message = QStringLiteral("%1 item(s) could not be processed:\n\n%2")
                          .arg(m_itemErrors.size())
                          .arg(QStringList(m_itemErrors.mid(0, shown)).join(QLatin1Char('\n')));
    if (m_itemErrors.size() > shown) {
        message += QStringLiteral("\n…");
    }
    if (result.errorMessage.isEmpty()) {
        result.errorMessage = message;
    } else {
        result.errorMessage += QLatin1Char('\n') + message;
    }
}

QString FileOperationEngine::generateDuplicateName(const QString& path) {
    QFileInfo fi(path);
    QString dir = fi.absolutePath();
    QString base = fi.completeBaseName();
    QString suffix = fi.suffix();

    QString candidate;
    int count = 1;
    do {
        QString copySuffix =
            (count == 1) ? QStringLiteral(" copy") : QStringLiteral(" copy %1").arg(count);
        if (!suffix.isEmpty()) {
            candidate = QStringLiteral("%1/%2%3.%4").arg(dir, base, copySuffix, suffix);
        } else {
            candidate = QStringLiteral("%1/%2%3").arg(dir, base, copySuffix);
        }
        count++;
    } while (QFile::exists(candidate));

    return candidate;
}

QString FileOperationEngine::generateKeepBothName(const QString& destinationPath) {
    return generateDuplicateName(destinationPath);
}

ConflictResolution FileOperationEngine::resolveFolderConflict(const QString& src,
                                                              const QString& dst) {
    if (m_folderApplyToAll && m_folderResolution != ConflictResolution::Ask) {
        return m_folderResolution;
    }

    OperationConflict conflict;
    conflict.sourcePath = src;
    conflict.destinationPath = dst;
    QFileInfo srcInfo(src);
    QFileInfo dstInfo(dst);
    conflict.sourceSize = srcInfo.size();
    conflict.destinationSize = dstInfo.size();
    conflict.sourceModified = srcInfo.lastModified();
    conflict.destinationModified = dstInfo.lastModified();
    conflict.sourceIsDir = srcInfo.isDir();
    conflict.destinationIsDir = dstInfo.isDir() && !dstInfo.isSymLink();

    ConflictResolution resolution = m_folderResolution;
    bool applyToAll = m_folderApplyToAll;
    QMetaObject::invokeMethod(
        this,
        [this, conflict, &resolution, &applyToAll]() {
            emit conflictEncountered(conflict, resolution, applyToAll);
        },
        Qt::BlockingQueuedConnection);

    if (resolution == ConflictResolution::Ask) {
        resolution = ConflictResolution::Cancel;
    }
    if (applyToAll) {
        m_folderResolution = resolution;
        m_folderApplyToAll = true;
    }
    return resolution;
}

bool FileOperationEngine::copyItems(const QStringList& sources, const QString& destinationDir,
                                    ConflictResolution res) {
    return startWorker([this, sources, destinationDir, res]() {
        executeCopyOrMove(OperationType::Copy, sources, destinationDir, res);
    });
}

bool FileOperationEngine::moveItems(const QStringList& sources, const QString& destinationDir,
                                    ConflictResolution res) {
    return startWorker([this, sources, destinationDir, res]() {
        executeCopyOrMove(OperationType::Move, sources, destinationDir, res);
    });
}

bool FileOperationEngine::duplicateItems(const QStringList& sources) {
    return startWorker([this, sources]() { executeDuplicate(sources); });
}

bool FileOperationEngine::trashItems(const QStringList& sources) {
    return startWorker([this, sources]() { executeTrash(sources); });
}

bool FileOperationEngine::deletePermanently(const QStringList& sources) {
    return startWorker([this, sources]() { executeDelete(sources); });
}

bool FileOperationEngine::compressItems(const QStringList& sources,
                                        const QString& destinationArchive, int compressionLevel,
                                        const QString& passphrase) {
    return startWorker([this, sources, destinationArchive, compressionLevel, passphrase]() {
        executeCompress(sources, destinationArchive, compressionLevel, passphrase);
    });
}

bool FileOperationEngine::extractArchive(const QString& archivePath, const QString& destinationDir,
                                         const QString& passphrase) {
    return startWorker([this, archivePath, destinationDir, passphrase]() {
        executeExtract(archivePath, destinationDir, passphrase);
    });
}

bool FileOperationEngine::createFolder(const QString& parentDir, const QString& folderName,
                                       QString* createdPath, QString* error) {
    QDir dir(parentDir);
    QString newPath = dir.filePath(folderName);
    if (QFile::exists(newPath)) {
        if (error)
            *error = QStringLiteral("A folder or file with this name already exists.");
        return false;
    }
    if (!dir.mkdir(folderName)) {
        if (error)
            *error = QStringLiteral("Failed to create folder. Permission denied or invalid name.");
        return false;
    }
    if (createdPath)
        *createdPath = newPath;
    return true;
}

bool FileOperationEngine::createFile(const QString& parentDir, const QString& fileName,
                                     QString* createdPath, QString* error) {
    QDir dir(parentDir);
    QString newPath = dir.filePath(fileName);
    if (QFile::exists(newPath)) {
        if (error)
            *error = QStringLiteral("A file with this name already exists.");
        return false;
    }
    QFile file(newPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();
    if (createdPath)
        *createdPath = newPath;
    return true;
}

bool FileOperationEngine::renameItem(const QString& sourcePath, const QString& newName,
                                     QString* renamedPath, QString* error) {
    QFileInfo fi(sourcePath);
    if (!fi.exists() && !fi.isSymLink()) {
        if (error)
            *error = QStringLiteral("Source item does not exist.");
        return false;
    }
    QString destPath = fi.dir().filePath(newName);
    if (QFile::exists(destPath) || QFileInfo(destPath).isSymLink()) {
        if (error)
            *error = QStringLiteral("Destination item already exists.");
        return false;
    }
    if (!QFile::rename(sourcePath, destPath)) {
        if (error)
            *error = QStringLiteral("Failed to rename item. Permission denied or file in use.");
        return false;
    }
    if (renamedPath)
        *renamedPath = destPath;
    return true;
}

bool FileOperationEngine::createSymlink(const QString& target, const QString& linkPath,
                                        QString* error) {
    if (QFile::exists(linkPath) || QFileInfo(linkPath).isSymLink()) {
        if (error)
            *error = QStringLiteral("Link path already exists.");
        return false;
    }
    if (!QFile::link(target, linkPath)) {
        if (error)
            *error = QStringLiteral("Failed to create symlink.");
        return false;
    }
    return true;
}

void FileOperationEngine::executeCopyOrMove(OperationType type, const QStringList& sources,
                                            const QString& destDir, ConflictResolution defaultRes) {
    // A same-filesystem move is a rename, so walking the whole source tree
    // first is wasted work. Cross-device moves still need the full estimate.
    qint64 totalBytes = 0;
    int totalItems = 0;
    bool sameDeviceMove = (type == OperationType::Move) && !sources.isEmpty();
    struct stat destStat{};
    if (sameDeviceMove) {
        sameDeviceMove = ::lstat(QFile::encodeName(destDir).constData(), &destStat) == 0;
    }
    if (sameDeviceMove) {
        for (const QString& src : sources) {
            struct stat srcStat{};
            if (::lstat(QFile::encodeName(src).constData(), &srcStat) != 0 ||
                srcStat.st_dev != destStat.st_dev) {
                sameDeviceMove = false;
                break;
            }
        }
    }

    for (const auto& src : sources) {
        QFileInfo fi(src);
        if (sameDeviceMove) {
            ++totalItems;
            totalBytes += fi.size();
            continue;
        }
        if (fi.isDir()) {
            QDirIterator it(src,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
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
    m_currentProgress.itemsCancelled = 0;
    m_currentProgress.bytesCancelled = 0;
    resetProgressTiming();

    const QVector<quint64> itemIds = beginItems(sources, type);
    ConflictResolution appliedResolution = defaultRes;
    bool applyToAll = (defaultRes != ConflictResolution::Ask);

    OperationResult result;
    result.type = type;
    QDir targetDir(destDir);

    for (int sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
        const QString& src = sources.at(sourceIndex);
        const quint64 itemId = itemIds.value(sourceIndex);
        if (isItemCancelled(itemId)) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            m_currentProgress.itemsProcessed++;
            {
                QMutexLocker locker(&m_itemMutex);
                m_currentProgress.bytesCancelled += m_itemInfo.value(itemId).totalBytes;
            }
            continue;
        }
        m_activeItemId.store(itemId, std::memory_order_relaxed);
        waitIfPaused();
        if (m_cancelled.load(std::memory_order_relaxed)) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("operation stopped"));
            result.success = false;
            result.errorMessage = QStringLiteral("Operation cancelled by user.");
            break;
        }
        if (activeItemCancelled()) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            m_currentProgress.itemsProcessed++;
            m_activeItemId.store(0, std::memory_order_relaxed);
            continue;
        }

        setItemStatus(itemId,
                      type == OperationType::Copy ? OperationItemStatus::Copying
                                                  : OperationItemStatus::Moving,
                      src);
        QString cleanSrc = QDir::cleanPath(src);
        QString cleanDestDir = QDir::cleanPath(destDir);
        QFileInfo srcInfo(cleanSrc);

        // Prevent copying or moving a directory into itself or its own subdirectories
        if ((srcInfo.isDir() && !srcInfo.isSymLink()) &&
            (cleanDestDir == cleanSrc || cleanDestDir.startsWith(cleanSrc + QLatin1Char('/')))) {
            recordError(src,
                        QStringLiteral("cannot %1 a folder into itself or a subfolder of itself")
                            .arg(type == OperationType::Copy ? QStringLiteral("copy")
                                                             : QStringLiteral("move")));
            continue;
        }

        QString destPath = targetDir.filePath(srcInfo.fileName());

        ItemOutcome outcome = ItemOutcome::Failed;
        if (type == OperationType::Copy) {
            if (srcInfo.isDir() && !srcInfo.isSymLink()) {
                outcome = copyDirectoryRecursive(src, destPath, appliedResolution, applyToAll);
            } else {
                outcome = copySingleFile(src, destPath, appliedResolution, applyToAll);
            }
        } else { // Move
            outcome = moveSingleItem(src, destPath, appliedResolution, applyToAll);
        }

        if (outcome == ItemOutcome::Done) {
            result.processedCount++;
            result.createdPaths.append(destPath);
            setItemStatus(itemId, OperationItemStatus::Done, destPath);
            if (type == OperationType::Move) {
                result.movedPairs.append(qMakePair(src, destPath));
            }
        } else if (outcome == ItemOutcome::Failed) {
            result.errorCount++;
            setItemStatus(itemId, OperationItemStatus::Failed, QStringLiteral("failed"));
            // Skipped items are deliberately not reported as created or moved:
            // undo must never act on a path that was left untouched.
        } else if (outcome == ItemOutcome::Skipped) {
            setItemStatus(itemId, OperationItemStatus::Skipped, QStringLiteral("skipped"));
        } else if (outcome == ItemOutcome::Cancelled) {
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            m_currentProgress.itemsProcessed++;
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
        }
        m_activeItemId.store(0, std::memory_order_relaxed);
    }

    if (m_currentProgress.itemsCancelled > 0 && m_currentProgress.totalItems > 0) {
        m_currentProgress.percent =
            (m_currentProgress.itemsProcessed * 100) / m_currentProgress.totalItems;
    }
    if (m_cancelled.load(std::memory_order_relaxed) && result.errorMessage.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("Operation cancelled by user.");
    }
    if (result.errorCount > 0) {
        result.success = false;
    }
    applyCollectedIssues(result);
    emit operationFinished(result);
}

FileOperationEngine::ItemOutcome FileOperationEngine::copySingleFile(const QString& src,
                                                                     const QString& dst,
                                                                     ConflictResolution& appliedRes,
                                                                     bool& applyToAll) {
    QFileInfo srcFi(src);
    if (!srcFi.exists() && !srcFi.isSymLink()) {
        recordError(src, QStringLiteral("source no longer exists"));
        return ItemOutcome::Failed;
    }

    if (m_skipCurrentFile.exchange(false)) {
        recordWarning(src, QStringLiteral("skipped by user"));
        m_skippedItems++;
        return ItemOutcome::Skipped;
    }
    if (activeItemCancelled())
        return ItemOutcome::Cancelled;

    if (samePath(src, dst) || sameInode(src, dst)) {
        recordWarning(src, QStringLiteral("source and destination are the same item; skipped"));
        m_skippedItems++;
        return ItemOutcome::Skipped;
    }

    struct stat st{};
    const QByteArray srcBytes = QFile::encodeName(src);
    if (::lstat(srcBytes.constData(), &st) != 0) {
        recordError(src, QString::fromLocal8Bit(std::strerror(errno)));
        return ItemOutcome::Failed;
    }

    QString finalDst = dst;
    if (QFile::exists(finalDst) || QFileInfo(finalDst).isSymLink()) {
        if (!applyToAll || appliedRes == ConflictResolution::Ask) {
            OperationConflict conflict;
            conflict.sourcePath = src;
            conflict.destinationPath = finalDst;
            QFileInfo dstFi(finalDst);
            conflict.sourceSize = srcFi.size();
            conflict.destinationSize = dstFi.size();
            conflict.sourceModified = srcFi.lastModified();
            conflict.destinationModified = dstFi.lastModified();
            conflict.sourceIsDir = srcFi.isDir();
            conflict.destinationIsDir = dstFi.isDir() && !dstFi.isSymLink();

            // Ask on main thread
            QMetaObject::invokeMethod(
                this,
                [this, conflict, &appliedRes, &applyToAll]() {
                    emit conflictEncountered(conflict, appliedRes, applyToAll);
                },
                Qt::BlockingQueuedConnection);
        }

        if (appliedRes == ConflictResolution::Cancel) {
            cancel();
            return ItemOutcome::Failed;
        } else if (appliedRes == ConflictResolution::Skip) {
            m_skippedItems++;
            return ItemOutcome::Skipped;
        } else if (appliedRes == ConflictResolution::KeepBoth) {
            finalDst = generateKeepBothName(finalDst);
        }
        // Replace is handled by installTemporary(): the existing destination is
        // not removed until the replacement has been written and verified.
    }

    const QFileInfo dstInfo(finalDst);
    m_currentProgress.currentItem = finalDst;
    const QString tempPath =
        uniqueSiblingPath(dstInfo.absolutePath(), QStringLiteral(".finder_tmp_"));
    QString installError;

    if (S_ISLNK(st.st_mode)) {
        const QByteArray target = QFile::encodeName(srcFi.symLinkTarget());
        const QByteArray tempBytes = QFile::encodeName(tempPath);
        if (::symlink(target.constData(), tempBytes.constData()) != 0) {
            recordError(src, QString::fromLocal8Bit(std::strerror(errno)));
            return ItemOutcome::Failed;
        }
        copyMetadata(src, tempPath, st, true);
        if (!installTemporary(tempPath, finalDst, &installError)) {
            QFile::remove(tempPath);
            recordError(finalDst, installError);
            return ItemOutcome::Failed;
        }
        m_currentProgress.itemsProcessed++;
        updateProgressTiming();
        emit progress(m_currentProgress);
        return ItemOutcome::Done;
    }

    if (S_ISFIFO(st.st_mode)) {
        const QByteArray tempBytes = QFile::encodeName(tempPath);
        if (::mkfifo(tempBytes.constData(), st.st_mode & 07777) != 0) {
            recordError(src, QString::fromLocal8Bit(std::strerror(errno)));
            return ItemOutcome::Failed;
        }
        copyMetadata(src, tempPath, st, false);
        if (!installTemporary(tempPath, finalDst, &installError)) {
            QFile::remove(tempPath);
            recordError(finalDst, installError);
            return ItemOutcome::Failed;
        }
        m_currentProgress.itemsProcessed++;
        updateProgressTiming();
        emit progress(m_currentProgress);
        return ItemOutcome::Done;
    }

    if (!S_ISREG(st.st_mode)) {
        // Never open sockets or device nodes: open() may block forever or
        // produce an endless stream of data.
        const QString reason = QStringLiteral("skipped special file (socket or device)");
        if (m_failOnSkip) {
            recordError(src, reason);
            return ItemOutcome::Failed;
        }
        recordWarning(src, reason);
        m_skippedItems++;
        return ItemOutcome::Skipped;
    }

    QFile srcFile(src);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        recordError(src, srcFile.errorString());
        return ItemOutcome::Failed;
    }

    // Write to a uniquely named temporary file in the destination directory.
    QFile dstFile(tempPath);
    if (!dstFile.open(QIODevice::WriteOnly)) {
        recordError(finalDst, dstFile.errorString());
        return ItemOutcome::Failed;
    }

    constexpr qint64 ChunkSize = 1024 * 1024; // 1 MB
    QByteArray buffer(ChunkSize, Qt::Uninitialized);

    while (!srcFile.atEnd()) {
        waitIfPaused();
        if (m_cancelled.load(std::memory_order_relaxed)) {
            dstFile.close();
            QFile::remove(tempPath);
            return ItemOutcome::Failed;
        }
        if (activeItemCancelled()) {
            dstFile.close();
            QFile::remove(tempPath);
            return ItemOutcome::Cancelled;
        }
        if (m_skipCurrentFile.exchange(false)) {
            dstFile.close();
            QFile::remove(tempPath);
            recordWarning(src, QStringLiteral("skipped by user"));
            m_skippedItems++;
            return ItemOutcome::Skipped;
        }

        qint64 bytesRead = srcFile.read(buffer.data(), ChunkSize);
        if (bytesRead > 0) {
            qint64 bytesWritten = dstFile.write(buffer.data(), bytesRead);
            if (bytesWritten != bytesRead) {
                const QString reason = dstFile.errorString();
                dstFile.close();
                QFile::remove(tempPath);
                recordError(finalDst, reason);
                return ItemOutcome::Failed;
            }
            m_currentProgress.bytesProcessed += bytesRead;

            m_currentProgress.percent = m_currentProgress.totalBytes > 0
                                            ? static_cast<int>(((m_currentProgress.bytesProcessed +
                                                                 m_currentProgress.bytesCancelled) *
                                                                100) /
                                                               m_currentProgress.totalBytes)
                                            : 100;
            m_currentProgress.currentItem = finalDst;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastProgressEmitMs >= 100) {
                updateProgressTiming();
                emit progress(m_currentProgress);
                m_lastProgressEmitMs = now;
            }
        }
    }

    dstFile.flush();
    if (dstFile.error() != QFileDevice::NoError) {
        const QString reason = dstFile.errorString();
        dstFile.close();
        QFile::remove(tempPath);
        recordError(finalDst, reason);
        return ItemOutcome::Failed;
    }
    // Durability barrier before the rename, so a power loss cannot leave a
    // zero-length file where the original used to be.
    if (dstFile.handle() >= 0) {
        ::fsync(dstFile.handle());
    }
    dstFile.close();
    srcFile.close();

    // Verify temp destination size matches source
    if (QFileInfo(tempPath).size() != static_cast<qint64>(st.st_size)) {
        QFile::remove(tempPath);
        recordError(finalDst, QStringLiteral("size verification failed"));
        return ItemOutcome::Failed;
    }

    // Preserve permissions, ownership, ACLs/xattrs, and timestamps.
    copyMetadata(src, tempPath, st, false);

    if (!installTemporary(tempPath, finalDst, &installError)) {
        QFile::remove(tempPath);
        recordError(finalDst, installError);
        return ItemOutcome::Failed;
    }

    // Verify final destination exists and size matches
    QFileInfo verifyFi(finalDst);
    if (!verifyFi.exists() || verifyFi.size() != static_cast<qint64>(st.st_size)) {
        recordError(finalDst, QStringLiteral("size verification failed after copy"));
        return ItemOutcome::Failed;
    }

    m_currentProgress.itemsProcessed++;
    updateProgressTiming();
    emit progress(m_currentProgress);
    return ItemOutcome::Done;
}

FileOperationEngine::ItemOutcome
FileOperationEngine::copyDirectoryRecursive(const QString& src, const QString& dst,
                                            ConflictResolution& appliedRes, bool& applyToAll,
                                            bool mergeIntoExisting) {
    const QString cleanSrc = QDir::cleanPath(src);
    QString finalDst = QDir::cleanPath(dst);
    if (finalDst == cleanSrc || finalDst.startsWith(cleanSrc + QLatin1Char('/'))) {
        recordError(src, QStringLiteral("cannot copy a folder into itself or a subfolder"));
        return ItemOutcome::Failed;
    }
    if (activeItemCancelled())
        return ItemOutcome::Cancelled;

    struct stat st{};
    if (::lstat(QFile::encodeName(src).constData(), &st) != 0) {
        recordError(src, QString::fromLocal8Bit(std::strerror(errno)));
        return ItemOutcome::Failed;
    }

    QString backupPath;
    QFileInfo dstInfo(finalDst);
    bool destinationExists = dstInfo.exists() || dstInfo.isSymLink();
    bool merge = mergeIntoExisting && destinationExists;

    if (destinationExists && !mergeIntoExisting) {
        const ConflictResolution resolution = resolveFolderConflict(src, finalDst);
        if (resolution == ConflictResolution::Cancel) {
            cancel();
            return ItemOutcome::Failed;
        }
        if (resolution == ConflictResolution::Skip) {
            m_skippedItems++;
            return ItemOutcome::Skipped;
        }
        if (resolution == ConflictResolution::KeepBoth) {
            finalDst = generateKeepBothName(finalDst);
            dstInfo = QFileInfo(finalDst);
            destinationExists = false;
        } else if (resolution == ConflictResolution::Merge) {
            if (dstInfo.isDir() && !dstInfo.isSymLink()) {
                merge = true;
            } else {
                // Merge is only valid for folders; replace a file conflict.
                backupPath =
                    uniqueSiblingPath(dstInfo.absolutePath(), QStringLiteral(".finder_old_"));
                if (!posixRename(finalDst, backupPath)) {
                    recordError(finalDst, QString::fromLocal8Bit(std::strerror(errno)));
                    return ItemOutcome::Failed;
                }
                destinationExists = false;
            }
        } else if (resolution == ConflictResolution::Replace) {
            backupPath = uniqueSiblingPath(dstInfo.absolutePath(), QStringLiteral(".finder_old_"));
            if (!posixRename(finalDst, backupPath)) {
                recordError(finalDst, QString::fromLocal8Bit(std::strerror(errno)));
                return ItemOutcome::Failed;
            }
            destinationExists = false;
        }
    } else if (!mergeIntoExisting && destinationExists && dstInfo.isDir() && !dstInfo.isSymLink()) {
        merge = true;
    }

    if (!merge && !QDir().mkpath(finalDst)) {
        if (!backupPath.isEmpty()) {
            posixRename(backupPath, finalDst);
        }
        recordError(finalDst, QStringLiteral("could not create destination folder"));
        return ItemOutcome::Failed;
    }

    QDir srcDir(src);
    for (const auto& entry : srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries |
                                                  QDir::Hidden | QDir::System)) {
        waitIfPaused();
        if (m_cancelled.load(std::memory_order_relaxed)) {
            if (!backupPath.isEmpty()) {
                QDir(finalDst).removeRecursively();
                posixRename(backupPath, finalDst);
            }
            return ItemOutcome::Failed;
        }
        if (activeItemCancelled()) {
            if (!backupPath.isEmpty()) {
                QDir(finalDst).removeRecursively();
                posixRename(backupPath, finalDst);
            }
            return ItemOutcome::Cancelled;
        }

        const QString srcItem = entry.absoluteFilePath();
        const QString dstItem = finalDst + QLatin1Char('/') + entry.fileName();

        ItemOutcome outcome = ItemOutcome::Failed;
        if (entry.isDir() && !entry.isSymLink()) {
            outcome = copyDirectoryRecursive(srcItem, dstItem, appliedRes, applyToAll);
        } else {
            outcome = copySingleFile(srcItem, dstItem, appliedRes, applyToAll);
        }

        if (outcome == ItemOutcome::Failed) {
            if (!backupPath.isEmpty()) {
                QDir(finalDst).removeRecursively();
                posixRename(backupPath, finalDst);
            }
            return ItemOutcome::Failed;
        }
    }

    // mkpath() applies the process umask; restore the source permissions and
    // metadata so private folders and ACLs survive a copy.
    copyMetadata(src, finalDst, st, false);

    if (!backupPath.isEmpty()) {
        QDir(backupPath).removeRecursively();
    }

    return ItemOutcome::Done;
}

FileOperationEngine::ItemOutcome FileOperationEngine::moveSingleItem(const QString& src,
                                                                     const QString& dst,
                                                                     ConflictResolution& appliedRes,
                                                                     bool& applyToAll) {
    QFileInfo srcInfo(src);
    if (!srcInfo.exists() && !srcInfo.isSymLink()) {
        recordError(src, QStringLiteral("source no longer exists"));
        return ItemOutcome::Failed;
    }

    waitIfPaused();
    if (m_skipCurrentFile.exchange(false)) {
        recordWarning(src, QStringLiteral("skipped by user"));
        m_skippedItems++;
        return ItemOutcome::Skipped;
    }
    if (activeItemCancelled())
        return ItemOutcome::Cancelled;

    QString cleanSrc = QDir::cleanPath(src);
    QString cleanDst = QDir::cleanPath(dst);

    // Prevent moving directory into itself
    if (srcInfo.isDir() && !srcInfo.isSymLink() &&
        (cleanDst == cleanSrc || cleanDst.startsWith(cleanSrc + QLatin1Char('/')))) {
        recordError(src, QStringLiteral("cannot move a folder into itself or a subfolder"));
        return ItemOutcome::Failed;
    }

    if (samePath(src, dst) || sameInode(src, dst)) {
        recordWarning(src, QStringLiteral("source and destination are the same item; skipped"));
        m_skippedItems++;
        return ItemOutcome::Skipped;
    }

    QString finalDst = dst;
    m_currentProgress.currentItem = finalDst;
    bool folderConflictResolved = false;
    if (srcInfo.isDir() && !srcInfo.isSymLink() &&
        (QFile::exists(finalDst) || QFileInfo(finalDst).isSymLink())) {
        folderConflictResolved = true;
        const QFileInfo existingDst(finalDst);
        const ConflictResolution folderResolution = resolveFolderConflict(src, finalDst);
        if (folderResolution == ConflictResolution::Cancel) {
            cancel();
            return ItemOutcome::Failed;
        }
        if (folderResolution == ConflictResolution::Skip) {
            m_skippedItems++;
            return ItemOutcome::Skipped;
        }
        if (folderResolution == ConflictResolution::KeepBoth) {
            finalDst = generateKeepBothName(finalDst);
        } else if (folderResolution == ConflictResolution::Merge && existingDst.isDir() &&
                   !existingDst.isSymLink()) {
            const int skippedBefore = m_skippedItems;
            const ItemOutcome copyOutcome =
                copyDirectoryRecursive(src, finalDst, appliedRes, applyToAll, true);
            if (copyOutcome == ItemOutcome::Cancelled)
                return ItemOutcome::Cancelled;
            if (copyOutcome != ItemOutcome::Done || m_skippedItems != skippedBefore ||
                m_cancelled.load(std::memory_order_relaxed)) {
                if (!m_cancelled.load(std::memory_order_relaxed)) {
                    recordError(
                        src, QStringLiteral("folder merge incomplete; the source folder was kept"));
                }
                return ItemOutcome::Failed;
            }
            if (!QDir(src).removeRecursively()) {
                recordError(src,
                            QStringLiteral("merged, but the source folder could not be removed"));
                return ItemOutcome::Failed;
            }
            m_currentProgress.itemsProcessed++;
            updateProgressTiming();
            emit progress(m_currentProgress);
            return ItemOutcome::Done;
        }
    }

    if (!folderConflictResolved && (QFile::exists(finalDst) || QFileInfo(finalDst).isSymLink())) {
        if (!applyToAll || appliedRes == ConflictResolution::Ask) {
            OperationConflict conflict;
            conflict.sourcePath = src;
            conflict.destinationPath = finalDst;
            QFileInfo dstFi(finalDst);
            conflict.sourceSize = srcInfo.size();
            conflict.destinationSize = dstFi.size();
            conflict.sourceModified = srcInfo.lastModified();
            conflict.destinationModified = dstFi.lastModified();
            conflict.sourceIsDir = srcInfo.isDir();
            conflict.destinationIsDir = dstFi.isDir() && !dstFi.isSymLink();

            QMetaObject::invokeMethod(
                this,
                [this, conflict, &appliedRes, &applyToAll]() {
                    emit conflictEncountered(conflict, appliedRes, applyToAll);
                },
                Qt::BlockingQueuedConnection);
        }

        if (appliedRes == ConflictResolution::Cancel) {
            cancel();
            return ItemOutcome::Failed;
        } else if (appliedRes == ConflictResolution::Skip) {
            m_skippedItems++;
            return ItemOutcome::Skipped;
        } else if (appliedRes == ConflictResolution::KeepBoth) {
            finalDst = generateKeepBothName(finalDst);
        }
        // Replace: the destination is swapped aside only once the incoming item
        // is ready (rename replaces files atomically by itself).
    }

    if (activeItemCancelled())
        return ItemOutcome::Cancelled;

    errno = 0;
    if (posixRename(src, finalDst)) {
        syncDirectory(QFileInfo(finalDst).absolutePath());
        m_currentProgress.itemsProcessed++;
        updateProgressTiming();
        emit progress(m_currentProgress);
        return ItemOutcome::Done;
    }
    const int renameError = errno;

    if (m_cancelled.load(std::memory_order_relaxed)) {
        return ItemOutcome::Failed;
    }

    if (activeItemCancelled())
        return ItemOutcome::Cancelled;

    // Destination is an existing directory: swap it aside first so a failure
    // can restore it instead of leaving the user with neither item.
    QFileInfo dstInfo(finalDst);
    if (dstInfo.exists() && dstInfo.isDir() && !dstInfo.isSymLink()) {
        const QString backupPath =
            uniqueSiblingPath(dstInfo.absolutePath(), QStringLiteral(".finder_old_"));
        if (!posixRename(finalDst, backupPath)) {
            recordError(finalDst, QString::fromLocal8Bit(std::strerror(errno)));
            return ItemOutcome::Failed;
        }
        if (posixRename(src, finalDst)) {
            QDir(backupPath).removeRecursively();
            syncDirectory(dstInfo.absolutePath());
            m_currentProgress.itemsProcessed++;
            updateProgressTiming();
            emit progress(m_currentProgress);
            return ItemOutcome::Done;
        }
        const QString failure = QString::fromLocal8Bit(std::strerror(errno));
        posixRename(backupPath, finalDst);
        recordError(finalDst, failure);
        return ItemOutcome::Failed;
    }

    if (renameError != EXDEV) {
        recordError(src, QString::fromLocal8Bit(std::strerror(renameError)));
        return ItemOutcome::Failed;
    }

    // Cross-filesystem move: copy and verify first, delete the source only
    // after every item has been transferred successfully.
    m_failOnSkip = true;
    ItemOutcome copyOutcome = ItemOutcome::Failed;
    if (srcInfo.isDir() && !srcInfo.isSymLink()) {
        copyOutcome = copyDirectoryRecursive(src, finalDst, appliedRes, applyToAll);
    } else {
        copyOutcome = copySingleFile(src, finalDst, appliedRes, applyToAll);
    }
    m_failOnSkip = false;

    if (copyOutcome == ItemOutcome::Cancelled)
        return ItemOutcome::Cancelled;
    if (copyOutcome != ItemOutcome::Done || m_cancelled.load(std::memory_order_relaxed)) {
        // Anything less than a complete, verified copy leaves the source intact.
        if (copyOutcome == ItemOutcome::Skipped) {
            recordWarning(src, QStringLiteral("was not moved because the conflict was skipped"));
        } else if (!m_cancelled.load(std::memory_order_relaxed) && m_itemErrors.isEmpty()) {
            recordError(src, QStringLiteral("cross-filesystem move failed; the source was kept"));
        }
        return ItemOutcome::Failed;
    }

    QFileInfo copied(finalDst);
    if (srcInfo.isDir()) {
        if (!copied.isDir()) {
            recordError(finalDst, QStringLiteral("copy verification failed"));
            return ItemOutcome::Failed;
        }
        if (!QDir(src).removeRecursively()) {
            recordError(src, QStringLiteral("copied, but the source folder could not be removed"));
            return ItemOutcome::Failed;
        }
    } else if (srcInfo.isSymLink()) {
        if (!copied.isSymLink()) {
            recordError(finalDst, QStringLiteral("symlink verification failed"));
            return ItemOutcome::Failed;
        }
        if (!QFile::remove(src)) {
            recordError(src, QStringLiteral("copied, but the source link could not be removed"));
            return ItemOutcome::Failed;
        }
    } else {
        if (!copied.exists() || copied.size() != srcInfo.size()) {
            recordError(finalDst, QStringLiteral("size verification failed"));
            return ItemOutcome::Failed;
        }
        if (!QFile::remove(src)) {
            recordError(src, QStringLiteral("copied, but the source file could not be removed"));
            return ItemOutcome::Failed;
        }
    }

    m_currentProgress.itemsProcessed++;
    updateProgressTiming();
    emit progress(m_currentProgress);
    return ItemOutcome::Done;
}

void FileOperationEngine::executeDuplicate(const QStringList& sources) {
    emit operationStarted(OperationType::Duplicate, static_cast<int>(sources.size()), 0);
    OperationResult result;
    result.type = OperationType::Duplicate;
    ConflictResolution res = ConflictResolution::KeepBoth;
    bool applyAll = true;
    const QVector<quint64> itemIds = beginItems(sources, OperationType::Duplicate);

    for (int i = 0; i < sources.size(); ++i) {
        const QString& src = sources.at(i);
        const quint64 itemId = itemIds.value(i);
        if (isItemCancelled(itemId)) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            continue;
        }
        if (m_cancelled.load(std::memory_order_relaxed))
            break;
        m_activeItemId.store(itemId, std::memory_order_relaxed);
        setItemStatus(itemId, OperationItemStatus::Copying, src);
        QString dst = generateDuplicateName(src);
        QFileInfo fi(src);
        ItemOutcome outcome = ItemOutcome::Failed;
        if (fi.isDir() && !fi.isSymLink()) {
            outcome = copyDirectoryRecursive(src, dst, res, applyAll);
        } else {
            outcome = copySingleFile(src, dst, res, applyAll);
        }
        if (outcome == ItemOutcome::Done) {
            result.processedCount++;
            result.createdPaths.append(dst);
            setItemStatus(itemId, OperationItemStatus::Done, dst);
        } else if (outcome == ItemOutcome::Failed) {
            result.errorCount++;
            setItemStatus(itemId, OperationItemStatus::Failed, QStringLiteral("failed"));
        } else if (outcome == ItemOutcome::Skipped) {
            setItemStatus(itemId, OperationItemStatus::Skipped, QStringLiteral("skipped"));
        } else if (outcome == ItemOutcome::Cancelled) {
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
        }
        m_activeItemId.store(0, std::memory_order_relaxed);
    }
    if (m_cancelled.load(std::memory_order_relaxed) && result.errorMessage.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("Operation cancelled by user.");
    }
    if (result.errorCount > 0) {
        result.success = false;
    }
    applyCollectedIssues(result);
    emit operationFinished(result);
}

void FileOperationEngine::executeTrash(const QStringList& sources) {
    emit operationStarted(OperationType::Trash, static_cast<int>(sources.size()), 0);
    OperationResult result;
    result.type = OperationType::Trash;
    const QVector<quint64> itemIds = beginItems(sources, OperationType::Trash);
    for (int i = 0; i < sources.size(); ++i) {
        const QString& src = sources.at(i);
        const quint64 itemId = itemIds.value(i);
        if (isItemCancelled(itemId)) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            continue;
        }
        if (m_cancelled.load(std::memory_order_relaxed))
            break;
        m_activeItemId.store(itemId, std::memory_order_relaxed);
        if (activeItemCancelled()) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            m_activeItemId.store(0, std::memory_order_relaxed);
            continue;
        }
        setItemStatus(itemId, OperationItemStatus::Trashing, src);
        QString error;
        QString trashedFile;
        if (XdgTrash::moveToTrash(src, &error, &trashedFile)) {
            result.processedCount++;
            result.createdPaths.append(src);
            result.trashedPairs.append(qMakePair(src, trashedFile));
            setItemStatus(itemId, OperationItemStatus::Done, trashedFile);
        } else {
            result.errorCount++;
            recordError(src,
                        error.isEmpty() ? QStringLiteral("could not be moved to Trash") : error);
            setItemStatus(itemId, OperationItemStatus::Failed, error);
        }
        m_activeItemId.store(0, std::memory_order_relaxed);
    }
    if (m_cancelled.load(std::memory_order_relaxed) && result.errorMessage.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("Operation cancelled by user.");
    }
    if (result.errorCount > 0) {
        result.success = false;
    }
    applyCollectedIssues(result);
    emit operationFinished(result);
}

void FileOperationEngine::executeDelete(const QStringList& sources) {
    emit operationStarted(OperationType::Delete, static_cast<int>(sources.size()), 0);
    OperationResult result;
    result.type = OperationType::Delete;
    const QVector<quint64> itemIds = beginItems(sources, OperationType::Delete);
    for (int i = 0; i < sources.size(); ++i) {
        const QString& src = sources.at(i);
        const quint64 itemId = itemIds.value(i);
        if (isItemCancelled(itemId)) {
            setItemStatus(itemId, OperationItemStatus::Cancelled,
                          QStringLiteral("cancelled by user"));
            result.cancelledCount++;
            m_currentProgress.itemsCancelled++;
            continue;
        }
        if (m_cancelled.load(std::memory_order_relaxed))
            break;
        m_activeItemId.store(itemId, std::memory_order_relaxed);
        setItemStatus(itemId, OperationItemStatus::Deleting, src);
        QFileInfo fi(src);
        bool ok = false;
        if (fi.isDir() && !fi.isSymLink()) {
            ok = QDir(src).removeRecursively();
        } else {
            ok = QFile::remove(src);
        }
        if (ok) {
            result.processedCount++;
            setItemStatus(itemId, OperationItemStatus::Done, QString());
            // Deleting an item that lives inside the Trash must also remove its
            // metadata, otherwise it is never listed again and leaks forever.
            const QString trashFiles = QDir::cleanPath(XdgTrash::trashFilesDirectory());
            const QString clean = QDir::cleanPath(src);
            if (clean.startsWith(trashFiles + QLatin1Char('/'))) {
                QFile::remove(XdgTrash::trashInfoDirectory() + QLatin1Char('/') + fi.fileName() +
                              QStringLiteral(".trashinfo"));
            }
        } else {
            result.errorCount++;
            recordError(src, QStringLiteral("could not be permanently deleted"));
            setItemStatus(itemId, OperationItemStatus::Failed,
                          QStringLiteral("could not be permanently deleted"));
        }
        m_activeItemId.store(0, std::memory_order_relaxed);
    }
    if (m_cancelled.load(std::memory_order_relaxed) && result.errorMessage.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("Operation cancelled by user.");
    }
    applyCollectedIssues(result);
    emit operationFinished(result);
}

void FileOperationEngine::executeCompress(const QStringList& sources,
                                          const QString& destinationArchive, int compressionLevel,
                                          const QString& passphrase) {
    emit operationStarted(OperationType::Compress, static_cast<int>(sources.size()), 0);
    const QVector<quint64> itemIds = beginItems(sources, OperationType::Compress);
    for (quint64 id : itemIds)
        setItemStatus(id, OperationItemStatus::Compressing);

    QElapsedTimer timer;
    timer.start();
    m_lastTimeMs = 0;
    m_lastBytes = 0;

    OperationProgress prog;
    prog.type = OperationType::Compress;
    prog.currentItem = QFileInfo(destinationArchive).fileName();
    prog.totalItems = static_cast<int>(sources.size());

    QString errorMsg;
    bool success = ArchiveService::compress(
        sources, destinationArchive,
        [this, &prog, &timer](const QString& currentFile, qint64 processedBytes,
                              qint64 totalBytes) {
            prog.currentItem = currentFile;
            prog.bytesProcessed = processedBytes;
            prog.totalBytes = totalBytes;
            if (totalBytes > 0) {
                prog.percent = static_cast<int>((processedBytes * 100) / totalBytes);
            }
            qint64 now = timer.elapsed();
            if (now - m_lastTimeMs > 200) {
                qint64 bytesDelta = processedBytes - m_lastBytes;
                double timeDeltaSec = static_cast<double>(now - m_lastTimeMs) / 1000.0;
                if (timeDeltaSec > 0.0) {
                    prog.speedBytesPerSec = static_cast<double>(bytesDelta) / timeDeltaSec;
                }
                prog.elapsedMs = now;
                if (prog.speedBytesPerSec > 0.0 && totalBytes > processedBytes) {
                    prog.remainingMs = static_cast<qint64>(
                        (static_cast<double>(totalBytes - processedBytes) / prog.speedBytesPerSec) *
                        1000.0);
                }
                m_lastTimeMs = now;
                m_lastBytes = processedBytes;
                emit progress(prog);
            }
        },
        &m_cancelled, &errorMsg, compressionLevel, passphrase, &m_paused);

    OperationResult result;
    result.type = OperationType::Compress;
    result.success = success && !m_cancelled.load();
    if (!result.success) {
        result.errorMessage =
            errorMsg.isEmpty() ? QStringLiteral("Compression cancelled or failed.") : errorMsg;
        result.errorCount = 1;
        for (quint64 id : itemIds)
            setItemStatus(id, OperationItemStatus::Failed, errorMsg);
    } else {
        result.processedCount = static_cast<int>(sources.size());
        result.createdPaths.append(destinationArchive);
        for (quint64 id : itemIds)
            setItemStatus(id, OperationItemStatus::Done, destinationArchive);
    }

    emit operationFinished(result);
}

void FileOperationEngine::executeExtract(const QString& archivePath, const QString& destinationDir,
                                         const QString& passphrase) {
    const qint64 archiveSize = QFileInfo(archivePath).size();

    QVector<ArchiveEntryInfo> entries;
    QString listError;
    if (!ArchiveService::listEntries(archivePath, &entries, 5000, &listError) ||
        entries.isEmpty()) {
        entries.clear();
    }

    QStringList itemPaths;
    itemPaths.reserve(entries.size());
    for (const ArchiveEntryInfo& entry : entries)
        itemPaths.append(entry.path);
    if (itemPaths.isEmpty())
        itemPaths.append(QFileInfo(archivePath).fileName());

    emit operationStarted(OperationType::Extract, static_cast<int>(itemPaths.size()), archiveSize);
    const QVector<quint64> itemIds = beginItems(itemPaths, OperationType::Extract);
    QHash<QString, quint64> entryIds;
    entryIds.reserve(entries.size());
    for (int i = 0; i < entries.size() && i < itemIds.size(); ++i)
        entryIds.insert(entries.at(i).path, itemIds.at(i));

    QElapsedTimer timer;
    timer.start();
    m_lastTimeMs = 0;
    m_lastBytes = 0;

    OperationProgress prog;
    prog.type = OperationType::Extract;
    prog.currentItem = QFileInfo(archivePath).fileName();
    prog.totalItems = static_cast<int>(itemPaths.size());
    prog.totalBytes = archiveSize;

    OperationResult result;
    result.type = OperationType::Extract;
    QSet<quint64> cancelledItems;
    quint64 lastEntryId = 0;

    auto markCurrentEntry = [this, &entryIds, &lastEntryId](const QString& currentFile) {
        auto it = entryIds.constFind(currentFile);
        if (it == entryIds.constEnd())
            return;
        const quint64 id = it.value();
        if (lastEntryId != 0 && lastEntryId != id)
            setItemStatus(lastEntryId, OperationItemStatus::Done);
        setItemStatus(id, OperationItemStatus::Extracting, currentFile);
        lastEntryId = id;
    };

    QString errorMsg;
    QString warningMsg;
    bool success = ArchiveService::extract(
        archivePath, destinationDir,
        [this, &prog, &timer, archiveSize,
         &markCurrentEntry](const QString& currentFile, qint64 processedBytes, qint64 totalBytes) {
            markCurrentEntry(currentFile);
            prog.currentItem = currentFile;
            prog.bytesProcessed = processedBytes;
            const qint64 effectiveTotal = (totalBytes > 0) ? totalBytes : archiveSize;
            prog.totalBytes = effectiveTotal;
            if (effectiveTotal > 0) {
                prog.percent =
                    qBound(0, static_cast<int>((processedBytes * 100) / effectiveTotal), 100);
            }
            const qint64 now = timer.elapsed();
            if (now - m_lastTimeMs > 200) {
                const qint64 bytesDelta = processedBytes - m_lastBytes;
                const double timeDeltaSec = static_cast<double>(now - m_lastTimeMs) / 1000.0;
                if (timeDeltaSec > 0.0) {
                    prog.speedBytesPerSec = static_cast<double>(bytesDelta) / timeDeltaSec;
                }
                prog.elapsedMs = now;
                if (prog.speedBytesPerSec > 0.0 && effectiveTotal > processedBytes) {
                    prog.remainingMs =
                        static_cast<qint64>((static_cast<double>(effectiveTotal - processedBytes) /
                                             prog.speedBytesPerSec) *
                                            1000.0);
                }
                m_lastTimeMs = now;
                m_lastBytes = processedBytes;
                emit progress(prog);
            }
        },
        &m_cancelled, &errorMsg, &warningMsg, passphrase, &m_paused,
        [this, &entryIds, &cancelledItems](const QString& entryPath) {
            auto it = entryIds.constFind(entryPath);
            if (it == entryIds.constEnd())
                return false;
            const quint64 id = it.value();
            if (!isItemCancelled(id))
                return false;
            setItemStatus(id, OperationItemStatus::Cancelled, QStringLiteral("cancelled by user"));
            cancelledItems.insert(id);
            return true;
        });

    if (lastEntryId != 0 && !cancelledItems.contains(lastEntryId))
        setItemStatus(lastEntryId, OperationItemStatus::Done);

    result.success = success && !m_cancelled.load();
    result.cancelledCount = static_cast<int>(cancelledItems.size());
    if (!result.success) {
        result.errorMessage =
            errorMsg.isEmpty() ? QStringLiteral("Extraction cancelled or failed.") : errorMsg;
        result.errorCount = 1;
        for (quint64 id : itemIds)
            if (!cancelledItems.contains(id))
                setItemStatus(id, OperationItemStatus::Failed, errorMsg);
    } else {
        result.processedCount = static_cast<int>(itemIds.size()) - result.cancelledCount;
        result.createdPaths.append(destinationDir);
        for (quint64 id : itemIds) {
            if (!cancelledItems.contains(id))
                setItemStatus(id, OperationItemStatus::Done, destinationDir);
        }
        if (!warningMsg.isEmpty())
            result.warnings.append(warningMsg);
    }

    emit operationFinished(result);
}
