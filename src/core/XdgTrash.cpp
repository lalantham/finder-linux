#include "XdgTrash.h"
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QSettings>
#include <QUrl>
#include <QFile>
#include <QDateTime>

static QString s_customTrashDir;

void XdgTrash::setCustomTrashDirectory(const QString& path)
{
    s_customTrashDir = path;
}

QString XdgTrash::trashDirectory()
{
    if (!s_customTrashDir.isEmpty()) {
        return s_customTrashDir;
    }
    QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty()) {
        dataHome = QDir::homePath() + QStringLiteral("/.local/share");
    }
    return dataHome + QStringLiteral("/Trash");
}

QString XdgTrash::trashFilesDirectory()
{
    return trashDirectory() + QStringLiteral("/files");
}

QString XdgTrash::trashInfoDirectory()
{
    return trashDirectory() + QStringLiteral("/info");
}

bool XdgTrash::ensureTrashDirs()
{
    QDir dir;
    return dir.mkpath(trashFilesDirectory()) && dir.mkpath(trashInfoDirectory());
}

QString XdgTrash::generateUniqueTrashBaseName(const QString& originalFileName)
{
    QString filesDir = trashFilesDirectory();
    QString base = originalFileName;
    if (base.isEmpty()) base = QStringLiteral("item");

    QString candidate = base;
    int counter = 1;
    QFileInfo fi(base);
    QString namePart = fi.completeBaseName();
    QString suffixPart = fi.suffix();

    while (QFile::exists(filesDir + QStringLiteral("/") + candidate) ||
           QFile::exists(trashInfoDirectory() + QStringLiteral("/") + candidate + QStringLiteral(".trashinfo"))) {
        if (!suffixPart.isEmpty()) {
            candidate = QStringLiteral("%1 %2.%3").arg(namePart).arg(counter).arg(suffixPart);
        } else {
            candidate = QStringLiteral("%1 %2").arg(namePart).arg(counter);
        }
        counter++;
    }
    return candidate;
}

bool XdgTrash::moveToTrash(const QString& sourcePath, QString* errorMessage)
{
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("Source file does not exist: %1").arg(sourcePath);
        return false;
    }

    if (!ensureTrashDirs()) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to create XDG Trash directories.");
        return false;
    }

    QString fileName = sourceInfo.fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("item");
    }
    QString destBaseName = generateUniqueTrashBaseName(fileName);

    QString destFilePath = trashFilesDirectory() + QStringLiteral("/") + destBaseName;
    QString destInfoPath = trashInfoDirectory() + QStringLiteral("/") + destBaseName + QStringLiteral(".trashinfo");

    // Move file/dir to trash files dir
    bool moved = false;
    // Attempt standard atomic rename first
    if (QFile::rename(sourcePath, destFilePath)) {
        moved = true;
    } else {
        // Fallback for cross-filesystem move
        // Note: For directories across fs, copy recursively and then delete source
        if (sourceInfo.isDir()) {
            // Recursive directory copy
            auto copyDirRecursive = [](auto self, const QString& src, const QString& dst) -> bool {
                QDir srcDir(src);
                if (!QDir().mkpath(dst)) return false;
                for (const auto& entry : srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
                    QString srcEntry = entry.absoluteFilePath();
                    QString dstEntry = dst + QStringLiteral("/") + entry.fileName();
                    if (entry.isDir()) {
                        if (!self(self, srcEntry, dstEntry)) return false;
                    } else {
                        if (!QFile::copy(srcEntry, dstEntry)) return false;
                    }
                }
                return true;
            };

            if (copyDirRecursive(copyDirRecursive, sourcePath, destFilePath)) {
                QDir(sourcePath).removeRecursively();
                moved = true;
            }
        } else {
            if (QFile::copy(sourcePath, destFilePath)) {
                QFile::remove(sourcePath);
                moved = true;
            }
        }
    }

    if (!moved) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to move file to trash destination: %1").arg(destFilePath);
        return false;
    }

    // Write .trashinfo
    QFile infoFile(destInfoPath);
    if (!infoFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to write trashinfo metadata: %1").arg(destInfoPath);
        // Rollback
        QFile::rename(destFilePath, sourcePath);
        return false;
    }

    QTextStream out(&infoFile);
    out << "[Trash Info]\n";
    // Paths should be percent-encoded as per freedesktop spec
    QUrl url = QUrl::fromLocalFile(sourceInfo.canonicalFilePath().isEmpty() ? sourceInfo.absoluteFilePath() : sourceInfo.canonicalFilePath());
    out << "Path=" << url.path() << "\n";
    out << "DeletionDate=" << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddThh:mm:ss")) << "\n";
    infoFile.close();

    return true;
}

bool XdgTrash::restoreItem(const QString& trashFilePath, QString* errorMessage)
{
    QFileInfo fi(trashFilePath);
    QString baseName = fi.fileName();
    QString infoPath = trashInfoDirectory() + QStringLiteral("/") + baseName + QStringLiteral(".trashinfo");

    if (!QFile::exists(infoPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("Trashinfo metadata not found for %1").arg(baseName);
        return false;
    }

    QSettings settings(infoPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Trash Info"));
    QString originalPath = settings.value(QStringLiteral("Path")).toString();
    settings.endGroup();

    if (originalPath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Corrupted trashinfo file for %1").arg(baseName);
        return false;
    }

    // Decode url if needed
    if (originalPath.startsWith(QStringLiteral("/"))) {
        // already decoded or local path
    } else {
        originalPath = QUrl::fromPercentEncoding(originalPath.toUtf8());
    }

    // Ensure parent dir exists
    QFileInfo origFi(originalPath);
    QDir().mkpath(origFi.absolutePath());

    if (QFile::exists(originalPath)) {
        if (errorMessage) *errorMessage = QStringLiteral("Target already exists at %1. Restore cannot overwrite.").arg(originalPath);
        return false;
    }

    if (!QFile::rename(trashFilePath, originalPath)) {
        // Try copy and delete fallback
        bool copied = false;
        if (fi.isDir()) {
            auto copyDirRecursive = [](auto self, const QString& src, const QString& dst) -> bool {
                QDir srcDir(src);
                if (!QDir().mkpath(dst)) return false;
                for (const auto& entry : srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
                    QString srcEntry = entry.absoluteFilePath();
                    QString dstEntry = dst + QStringLiteral("/") + entry.fileName();
                    if (entry.isDir()) {
                        if (!self(self, srcEntry, dstEntry)) return false;
                    } else {
                        if (!QFile::copy(srcEntry, dstEntry)) return false;
                    }
                }
                return true;
            };
            if (copyDirRecursive(copyDirRecursive, trashFilePath, originalPath)) {
                QDir(trashFilePath).removeRecursively();
                copied = true;
            }
        } else {
            if (QFile::copy(trashFilePath, originalPath)) {
                QFile::remove(trashFilePath);
                copied = true;
            }
        }

        if (!copied) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to move item back to %1").arg(originalPath);
            return false;
        }
    }

    // Remove info file
    QFile::remove(infoPath);
    return true;
}

bool XdgTrash::emptyTrash(QString* errorMessage)
{
    ensureTrashDirs();
    QDir filesDir(trashFilesDirectory());
    QDir infoDir(trashInfoDirectory());

    bool success = true;
    for (const auto& entry : filesDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
        if (entry.isDir()) {
            if (!QDir(entry.absoluteFilePath()).removeRecursively()) success = false;
        } else {
            if (!QFile::remove(entry.absoluteFilePath())) success = false;
        }
    }

    for (const auto& entry : infoDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
        if (!QFile::remove(entry.absoluteFilePath())) success = false;
    }

    if (!success && errorMessage) {
        *errorMessage = QStringLiteral("Some items in trash could not be deleted.");
    }
    return success;
}

QVector<TrashItem> XdgTrash::listTrashItems()
{
    QVector<TrashItem> items;
    QDir infoDir(trashInfoDirectory());
    if (!infoDir.exists()) return items;

    QStringList filter;
    filter << QStringLiteral("*.trashinfo");
    for (const auto& infoFile : infoDir.entryInfoList(filter, QDir::Files)) {
        QString baseName = infoFile.completeBaseName();
        QString trashFilePath = trashFilesDirectory() + QStringLiteral("/") + baseName;

        QFileInfo targetFi(trashFilePath);
        if (!targetFi.exists()) continue;

        QSettings settings(infoFile.absoluteFilePath(), QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Trash Info"));
        QString originalPath = settings.value(QStringLiteral("Path")).toString();
        QString dateStr = settings.value(QStringLiteral("DeletionDate")).toString();
        settings.endGroup();

        TrashItem item;
        item.id = baseName;
        item.originalPath = originalPath;
        item.deletionDate = QDateTime::fromString(dateStr, QStringLiteral("yyyy-MM-ddThh:mm:ss"));
        item.trashFilePath = trashFilePath;
        item.trashInfoPath = infoFile.absoluteFilePath();
        item.isDir = targetFi.isDir();
        item.size = item.isDir ? 0 : targetFi.size();

        items.append(item);
    }
    return items;
}

qint64 XdgTrash::totalTrashSize()
{
    qint64 total = 0;
    QDirIterator it(trashFilesDirectory(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
