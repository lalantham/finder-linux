#include "XdgTrash.h"
#include <QStandardPaths>
#include <QStorageInfo>
#include <QDir>
#include <QDirIterator>
#include <QUrl>
#include <QFile>
#include <QDateTime>
#include <QSet>
#include <QRandomGenerator>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static QString s_customTrashDir;

namespace {

struct TrashRoot {
    QString dir;      // directory containing files/ and info/
    QString filesDir;
    QString infoDir;
};

QString uniqueRestorePath(const QString& path)
{
    if (!QFile::exists(path) && !QFileInfo(path).isSymLink()) {
        return path;
    }

    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int counter = 1; counter < 1000; ++counter) {
        QString candidateName;
        if (suffix.isEmpty()) {
            candidateName = counter == 1
                ? QStringLiteral("%1 (restored)").arg(baseName)
                : QStringLiteral("%1 (restored %2)").arg(baseName).arg(counter);
        } else {
            candidateName = counter == 1
                ? QStringLiteral("%1 (restored).%2").arg(baseName, suffix)
                : QStringLiteral("%1 (restored %2).%3").arg(baseName).arg(counter).arg(suffix);
        }

        const QString candidate = QDir(directory).filePath(candidateName);
        if (!QFile::exists(candidate) && !QFileInfo(candidate).isSymLink()) {
            return candidate;
        }
    }

    return QDir(directory).filePath(info.fileName() + QStringLiteral(" (restored)"));
}

TrashRoot makeRoot(const QString& dir)
{
    TrashRoot root;
    root.dir = dir;
    root.filesDir = dir + QStringLiteral("/files");
    root.infoDir = dir + QStringLiteral("/info");
    return root;
}

bool posixRename(const QString& from, const QString& to)
{
    const QByteArray fromBytes = QFile::encodeName(from);
    const QByteArray toBytes = QFile::encodeName(to);
    return ::rename(fromBytes.constData(), toBytes.constData()) == 0;
}

QString temporarySibling(const QString& path)
{
    return path + QStringLiteral(".part-%1-%2")
        .arg(::getpid())
        .arg(QRandomGenerator::global()->generate());
}

// Copies a single item and verifies it. Used only for cross-device transfers;
// the caller removes the source only after this returns true.
bool copyItemVerified(const QString& src, const QString& dst, QString* errorMessage)
{
    struct stat st {};
    if (::lstat(QFile::encodeName(src).constData(), &st) != 0) {
        if (errorMessage) *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    if (S_ISLNK(st.st_mode)) {
        const QByteArray target = QFile::encodeName(QFileInfo(src).symLinkTarget());
        if (::symlink(target.constData(), QFile::encodeName(dst).constData()) != 0) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        return true;
    }

    if (S_ISDIR(st.st_mode)) {
        if (::mkdir(QFile::encodeName(dst).constData(), st.st_mode & 07777) != 0) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        const QDir dir(src);
        for (const auto& entry : dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
            if (!copyItemVerified(entry.absoluteFilePath(), dst + QLatin1Char('/') + entry.fileName(), errorMessage)) {
                return false;
            }
        }
        return true;
    }

    if (S_ISFIFO(st.st_mode)) {
        if (::mkfifo(QFile::encodeName(dst).constData(), st.st_mode & 07777) != 0) {
            if (errorMessage) *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        return true;
    }

    if (!S_ISREG(st.st_mode)) {
        if (errorMessage) *errorMessage = QStringLiteral("Special files cannot be moved to Trash: %1").arg(src);
        return false;
    }

    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = in.errorString();
        return false;
    }
    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = out.errorString();
        return false;
    }

    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    while (!in.atEnd()) {
        const qint64 read = in.read(buffer.data(), buffer.size());
        if (read < 0) {
            if (errorMessage) *errorMessage = in.errorString();
            out.close();
            return false;
        }
        if (read == 0) break;
        if (out.write(buffer.constData(), read) != read) {
            if (errorMessage) *errorMessage = out.errorString();
            out.close();
            return false;
        }
    }
    out.flush();
    if (out.error() != QFileDevice::NoError) {
        if (errorMessage) *errorMessage = out.errorString();
        out.close();
        return false;
    }
    if (out.handle() >= 0) ::fsync(out.handle());
    out.close();
    in.close();

    if (QFileInfo(dst).size() != static_cast<qint64>(st.st_size)) {
        if (errorMessage) *errorMessage = QStringLiteral("Size verification failed for %1").arg(dst);
        return false;
    }
    ::chmod(QFile::encodeName(dst).constData(), st.st_mode & 07777);
    return true;
}

void removeQuietly(const QString& path)
{
    QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        QDir(path).removeRecursively();
    } else {
        QFile::remove(path);
    }
}

QString infoPathForTrashFile(const QString& trashFilePath)
{
    // <trash>/files/<name> -> <trash>/info/<name>.trashinfo
    const QDir filesDir = QFileInfo(trashFilePath).absoluteDir();
    const QString infoDir = filesDir.absolutePath() + QStringLiteral("/../info");
    return QDir::cleanPath(infoDir + QLatin1Char('/') + QFileInfo(trashFilePath).fileName()
                           + QStringLiteral(".trashinfo"));
}

bool readTrashInfo(const QString& infoPath, QString* originalPath, QDateTime* deletionDate)
{
    QFile file(infoPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    bool inTrashInfo = false;
    QString encodedPath;
    QString dateString;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('['))) {
            inTrashInfo = (line == QStringLiteral("[Trash Info]"));
            continue;
        }
        if (!inTrashInfo) continue;

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) continue;
        const QString key = line.left(equals);
        const QString value = line.mid(equals + 1);
        if (key == QStringLiteral("Path")) {
            encodedPath = value;
        } else if (key == QStringLiteral("DeletionDate")) {
            dateString = value;
        }
    }

    if (encodedPath.isEmpty()) {
        return false;
    }

    // Decode exactly once (QSettings would add its own quoting rules).
    if (originalPath) {
        *originalPath = QUrl::fromPercentEncoding(encodedPath.toUtf8());
    }
    if (deletionDate) {
        QDateTime parsed = QDateTime::fromString(dateString, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        if (!parsed.isValid()) {
            // Historical files written with the 12-hour format.
            parsed = QDateTime::fromString(dateString, QStringLiteral("yyyy-MM-ddThh:mm:ss"));
        }
        *deletionDate = parsed;
    }
    return true;
}

// Creates/validates a directory that must be owned by us and not a symlink.
bool ensurePrivateDir(const QString& path)
{
    struct stat st {};
    const QByteArray bytes = QFile::encodeName(path);
    if (::lstat(bytes.constData(), &st) == 0) {
        if (!S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) {
            return false;
        }
        ::chmod(bytes.constData(), 0700);
        return true;
    }
    if (::mkdir(bytes.constData(), 0700) != 0) {
        return false;
    }
    ::chmod(bytes.constData(), 0700);
    return true;
}

bool ensureTrashLayout(const QString& trashDir)
{
    if (!ensurePrivateDir(trashDir)) return false;
    if (!ensurePrivateDir(trashDir + QStringLiteral("/files"))) return false;
    if (!ensurePrivateDir(trashDir + QStringLiteral("/info"))) return false;
    return true;
}

QString mountTopDirectory(const QString& path)
{
    QFileInfo info(path);
    QString current;
    if (info.exists() && info.isDir() && !info.isSymLink()) {
        current = info.absoluteFilePath();
    } else {
        current = info.absolutePath();
    }

    struct stat st {};
    if (::lstat(QFile::encodeName(current).constData(), &st) != 0) {
        return {};
    }
    const dev_t device = st.st_dev;
    QString top = current;

    while (true) {
        const QString parent = QFileInfo(top).absolutePath();
        if (parent.isEmpty() || parent == top) break;
        struct stat parentStat {};
        if (::lstat(QFile::encodeName(parent).constData(), &parentStat) != 0) break;
        if (parentStat.st_dev != device) break;
        top = parent;
    }
    return top;
}

// Per spec: use $topdir/.Trash/$uid only if $topdir/.Trash exists with the
// sticky bit; otherwise use $topdir/.Trash-$uid. Fall back to the home trash
// when the filesystem cannot host one.
bool resolveTrashRoot(const QString& sourcePath, TrashRoot* root, QString* errorMessage)
{
    if (!s_customTrashDir.isEmpty()) {
        *root = makeRoot(s_customTrashDir);
        if (!ensureTrashLayout(root->dir)) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to create XDG Trash directories.");
            return false;
        }
        return true;
    }

    const TrashRoot home = makeRoot(XdgTrash::trashDirectory());
    const QString sourceTop = mountTopDirectory(sourcePath);
    const QString homeTop = mountTopDirectory(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));

    // Same filesystem as the home trash (or unknown): use the home trash.
    if (sourceTop.isEmpty() || homeTop.isEmpty() || sourceTop == homeTop) {
        *root = home;
        if (!ensureTrashLayout(home.dir)) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to create XDG Trash directories.");
            return false;
        }
        return true;
    }

    const QString uid = QString::number(::getuid());
    const QString sharedTrash = sourceTop + QStringLiteral("/.Trash");
    const QString perUserInShared = sharedTrash + QLatin1Char('/') + uid;

    struct stat st {};
    if (::lstat(QFile::encodeName(sharedTrash).constData(), &st) == 0
        && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {
        if (ensurePrivateDir(perUserInShared) && ensureTrashLayout(perUserInShared)) {
            *root = makeRoot(perUserInShared);
            return true;
        }
    }

    const QString perUser = sourceTop + QStringLiteral("/.Trash-") + uid;
    if (ensurePrivateDir(perUser) && ensureTrashLayout(perUser)) {
        *root = makeRoot(perUser);
        return true;
    }

    // The mount cannot host a trash directory (read-only, no permission):
    // use the home trash, which copies cross-device with verification.
    *root = home;
    if (!ensureTrashLayout(home.dir)) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to create XDG Trash directories.");
        return false;
    }
    return true;
}

QStringList mountedMountPoints()
{
    // QStorageInfo filters out mounts like /dev/shm, but a per-filesystem trash
    // may exist there. Parse the authoritative mount table instead. Note that
    // /proc files report size 0, so readAll() is required (QTextStream would
    // see EOF immediately).
    QStringList result;
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (!mounts.open(QIODevice::ReadOnly)) {
        for (const auto& storage : QStorageInfo::mountedVolumes()) {
            if (storage.isValid() && storage.isReady()) {
                result.append(storage.rootPath());
            }
        }
        return result;
    }

    const QByteArray data = mounts.readAll();
    const QList<QByteArray> lines = data.split('\n');
    for (const auto& rawLine : lines) {
        const QString line = QString::fromLocal8Bit(rawLine);
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 2) continue;

        QString mountPoint = fields.at(1);
        // The mount table octal-escapes spaces and other special characters.
        mountPoint.replace(QStringLiteral("\\040"), QStringLiteral(" "));
        mountPoint.replace(QStringLiteral("\\011"), QStringLiteral("\t"));
        mountPoint.replace(QStringLiteral("\\012"), QStringLiteral("\n"));
        mountPoint.replace(QStringLiteral("\\134"), QStringLiteral("\\"));

        if (!mountPoint.isEmpty() && !result.contains(mountPoint)) {
            result.append(mountPoint);
        }
    }
    return result;
}

QVector<TrashRoot> allTrashRoots()
{
    QVector<TrashRoot> roots;
    QSet<QString> seen;

    auto addRoot = [&](const QString& dir) {
        if (dir.isEmpty()) return;
        const QString clean = QDir::cleanPath(dir);
        if (seen.contains(clean)) return;

        // Only consider trash directories that belong to this user.
        struct stat st {};
        if (::lstat(QFile::encodeName(clean).constData(), &st) != 0) return;
        if (!S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) return;

        seen.insert(clean);
        roots.append(makeRoot(clean));
    };

    if (!s_customTrashDir.isEmpty()) {
        addRoot(s_customTrashDir);
        return roots;
    }

    addRoot(XdgTrash::trashDirectory());

    const QString uid = QString::number(::getuid());
    for (const auto& top : mountedMountPoints()) {
        addRoot(top + QStringLiteral("/.Trash/") + uid);
        addRoot(top + QStringLiteral("/.Trash-") + uid);
    }

    return roots;
}

QString uniqueBaseName(const QString& originalFileName, const QString& filesDir, const QString& infoDir)
{
    QString base = originalFileName;
    if (base.isEmpty()) base = QStringLiteral("item");

    QString candidate = base;
    int counter = 1;
    QFileInfo fi(base);
    QString namePart = fi.completeBaseName();
    QString suffixPart = fi.suffix();

    while (QFile::exists(filesDir + QLatin1Char('/') + candidate) ||
           QFile::exists(infoDir + QLatin1Char('/') + candidate + QStringLiteral(".trashinfo"))) {
        if (!suffixPart.isEmpty()) {
            candidate = QStringLiteral("%1 %2.%3").arg(namePart).arg(counter).arg(suffixPart);
        } else {
            candidate = QStringLiteral("%1 %2").arg(namePart).arg(counter);
        }
        counter++;
    }
    return candidate;
}

} // namespace

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
    return ensureTrashLayout(trashDirectory());
}

bool XdgTrash::moveToTrash(const QString& sourcePath, QString* errorMessage, QString* trashedFilePath)
{
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
        if (errorMessage) *errorMessage = QStringLiteral("Source file does not exist: %1").arg(sourcePath);
        return false;
    }

    TrashRoot root;
    if (!resolveTrashRoot(sourcePath, &root, errorMessage)) {
        return false;
    }

    QString fileName = sourceInfo.fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("item");
    }

    // Reserve a name by creating the .trashinfo first: the spec recommends
    // writing the metadata before moving the item, and NewOnly guarantees no
    // two operations claim the same name.
    QString destBaseName;
    QString destFilePath;
    QString destInfoPath;
    QFile infoFile;
    for (int attempt = 0; attempt < 100; ++attempt) {
        destBaseName = uniqueBaseName(fileName, root.filesDir, root.infoDir);
        destFilePath = root.filesDir + QLatin1Char('/') + destBaseName;
        destInfoPath = root.infoDir + QLatin1Char('/') + destBaseName + QStringLiteral(".trashinfo");

        infoFile.setFileName(destInfoPath);
        if (infoFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::NewOnly)) {
            break;
        }
        if (!QFile::exists(destInfoPath)) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to write trashinfo metadata: %1").arg(destInfoPath);
            return false;
        }
    }
    if (!infoFile.isOpen()) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not allocate a name in the Trash.");
        return false;
    }

    {
        QTextStream out(&infoFile);
        out << "[Trash Info]\n";
        // Freedesktop spec: exact original path, percent-encoded, slashes kept.
        out << "Path=" << QUrl::toPercentEncoding(sourceInfo.absoluteFilePath(), "/") << "\n";
        out << "DeletionDate=" << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) << "\n";
        infoFile.close();
        if (infoFile.error() != QFileDevice::NoError) {
            QFile::remove(destInfoPath);
            if (errorMessage) *errorMessage = QStringLiteral("Failed to write trashinfo metadata: %1").arg(destInfoPath);
            return false;
        }
    }

    auto rollbackInfo = [&destInfoPath]() {
        QFile::remove(destInfoPath);
    };

    // 1. Atomic move (the per-filesystem trash lives on the source's device).
    if (posixRename(sourcePath, destFilePath)) {
        if (trashedFilePath) *trashedFilePath = destFilePath;
        return true;
    }
    const int renameError = errno;
    if (renameError != EXDEV) {
        rollbackInfo();
        if (errorMessage) *errorMessage = QStringLiteral("Failed to move item to Trash: %1")
                                              .arg(QString::fromLocal8Bit(std::strerror(renameError)));
        return false;
    }

    // 2. Cross-device (home-trash fallback): copy into a temporary name inside
    //    the trash filesystem, verify it completely, then atomically publish it
    //    and only then remove the original.
    const QString tempPath = temporarySibling(destFilePath);
    QString copyError;
    if (!copyItemVerified(sourcePath, tempPath, &copyError)) {
        removeQuietly(tempPath);
        rollbackInfo();
        if (errorMessage) *errorMessage = QStringLiteral("Could not copy item to Trash: %1").arg(copyError);
        return false;
    }

    if (!posixRename(tempPath, destFilePath)) {
        const QString reason = QString::fromLocal8Bit(std::strerror(errno));
        removeQuietly(tempPath);
        rollbackInfo();
        if (errorMessage) *errorMessage = QStringLiteral("Could not finalise trash copy: %1").arg(reason);
        return false;
    }

    bool sourceRemoved = false;
    if (sourceInfo.isDir() && !sourceInfo.isSymLink()) {
        sourceRemoved = QDir(sourcePath).removeRecursively();
    } else {
        sourceRemoved = QFile::remove(sourcePath);
    }

    if (!sourceRemoved) {
        // Keep the source, undo the trash copy: never duplicate silently.
        removeQuietly(destFilePath);
        rollbackInfo();
        if (errorMessage) *errorMessage = QStringLiteral("The item was copied to Trash but the original could not be removed.");
        return false;
    }

    if (trashedFilePath) *trashedFilePath = destFilePath;
    return true;
}

bool XdgTrash::restoreItem(const QString& trashFilePath, QString* errorMessage,
                           QString* restoredPath, bool keepBothOnConflict)
{
    QFileInfo fi(trashFilePath);
    const QString infoPath = infoPathForTrashFile(trashFilePath);

    QString originalPath;
    if (!readTrashInfo(infoPath, &originalPath, nullptr) || originalPath.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Corrupted or missing trashinfo for %1").arg(fi.fileName());
        return false;
    }

    QString destinationPath = originalPath;
    if (QFile::exists(destinationPath) || QFileInfo(destinationPath).isSymLink()) {
        if (!keepBothOnConflict) {
            if (errorMessage) *errorMessage = QStringLiteral("Target already exists at %1. Restore cannot overwrite.")
                                                  .arg(originalPath);
            return false;
        }
        destinationPath = uniqueRestorePath(originalPath);
    }

    // Ensure parent dir exists
    QFileInfo origFi(destinationPath);
    QDir().mkpath(origFi.absolutePath());

    if (!posixRename(trashFilePath, destinationPath)) {
        if (errno != EXDEV) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to move item back to %1: %2")
                                                  .arg(destinationPath, QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }

        // Cross-device restore: verify the copy before deleting the trash copy.
        const QString tempPath = temporarySibling(destinationPath);
        QString copyError;
        if (!copyItemVerified(trashFilePath, tempPath, &copyError)) {
            removeQuietly(tempPath);
            if (errorMessage) *errorMessage = QStringLiteral("Failed to restore %1: %2").arg(destinationPath, copyError);
            return false;
        }
        if (!posixRename(tempPath, destinationPath)) {
            const QString reason = QString::fromLocal8Bit(std::strerror(errno));
            removeQuietly(tempPath);
            if (errorMessage) *errorMessage = QStringLiteral("Failed to restore %1: %2").arg(destinationPath, reason);
            return false;
        }
        removeQuietly(trashFilePath);
    }

    // Remove info file
    QFile::remove(infoPath);
    if (restoredPath) *restoredPath = destinationPath;
    return true;
}

bool XdgTrash::emptyTrash(QString* errorMessage)
{
    bool anyFailure = false;

    for (const auto& root : allTrashRoots()) {
        const QDir filesDir(root.filesDir);
        const QDir infoDir(root.infoDir);

        for (const auto& entry : filesDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
            if (entry.isDir() && !entry.isSymLink()) {
                if (!QDir(entry.absoluteFilePath()).removeRecursively()) anyFailure = true;
            } else {
                if (!QFile::remove(entry.absoluteFilePath())) anyFailure = true;
            }
        }
        for (const auto& entry : infoDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
            if (!QFile::remove(entry.absoluteFilePath())) anyFailure = true;
        }
    }

    if (anyFailure && errorMessage) {
        *errorMessage = QStringLiteral("Some items in trash could not be deleted.");
    }
    return !anyFailure;
}

int XdgTrash::purgeOlderThan(const QDateTime& cutoff, QString* errorMessage)
{
    int purged = 0;
    for (const TrashItem& item : listTrashItems()) {
        if (!item.deletionDate.isValid() || item.deletionDate >= cutoff) {
            continue;
        }

        const QFileInfo info(item.trashFilePath);
        bool removed = false;
        if (info.isDir() && !info.isSymLink()) {
            removed = QDir(item.trashFilePath).removeRecursively();
        } else {
            removed = QFile::remove(item.trashFilePath);
        }

        if (removed) {
            QFile::remove(item.trashInfoPath);
            ++purged;
        } else if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Could not delete %1").arg(item.trashFilePath);
        }
    }
    return purged;
}

QVector<TrashItem> XdgTrash::listTrashItems()
{
    QVector<TrashItem> items;
    QSet<QString> seenPaths;

    for (const auto& root : allTrashRoots()) {
        const QDir infoDir(root.infoDir);
        if (!infoDir.exists()) continue;

        for (const auto& infoFile : infoDir.entryInfoList({QStringLiteral("*.trashinfo")}, QDir::Files)) {
            const QString baseName = infoFile.completeBaseName();
            const QString trashFilePath = root.filesDir + QLatin1Char('/') + baseName;

            QFileInfo targetFi(trashFilePath);
            if (!targetFi.exists() && !targetFi.isSymLink()) continue;
            if (seenPaths.contains(trashFilePath)) continue;

            QString originalPath;
            QDateTime deletionDate;
            if (!readTrashInfo(infoFile.absoluteFilePath(), &originalPath, &deletionDate)) continue;

            TrashItem item;
            item.id = baseName;
            item.originalPath = originalPath;
            item.deletionDate = deletionDate;
            item.trashFilePath = trashFilePath;
            item.trashInfoPath = infoFile.absoluteFilePath();
            item.isDir = targetFi.isDir();
            item.size = item.isDir ? 0 : targetFi.size();

            seenPaths.insert(trashFilePath);
            items.append(item);
        }
    }
    return items;
}

qint64 XdgTrash::totalTrashSize()
{
    qint64 total = 0;
    for (const auto& root : allTrashRoots()) {
        QDirIterator it(root.filesDir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            total += it.fileInfo().size();
        }
    }
    return total;
}
