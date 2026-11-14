#include "FileItem.h"
#include "TagManager.h"
#include <QFileIconProvider>
#include <QHash>
#include <QLocale>
#include <QMutex>
#include <QDir>
#include <QStandardPaths>
#include <cmath>

namespace {

QMimeDatabase& sharedMimeDatabase()
{
    // Directory scans run on worker threads; give each thread its own database.
    thread_local QMimeDatabase database;
    return database;
}

QString userName(uid_t uid)
{
    static QMutex mutex;
    static QHash<uid_t, QString> cache;
    QMutexLocker locker(&mutex);

    const auto cached = cache.constFind(uid);
    if (cached != cache.cend()) {
        return *cached;
    }

    QString name = QString::number(uid);
    if (struct passwd* pw = getpwuid(uid); pw && pw->pw_name) {
        name = QString::fromLocal8Bit(pw->pw_name);
    }
    cache.insert(uid, name);
    return name;
}

QString groupName(gid_t gid)
{
    static QMutex mutex;
    static QHash<gid_t, QString> cache;
    QMutexLocker locker(&mutex);

    const auto cached = cache.constFind(gid);
    if (cached != cache.cend()) {
        return *cached;
    }

    QString name = QString::number(gid);
    if (struct group* gr = getgrgid(gid); gr && gr->gr_name) {
        name = QString::fromLocal8Bit(gr->gr_name);
    }
    cache.insert(gid, name);
    return name;
}

QHash<QString, QString> buildSpecialDirIcons()
{
    QHash<QString, QString> map;
    auto add = [&map](QStandardPaths::StandardLocation location, const QString& icon) {
        const QString path = QStandardPaths::writableLocation(location);
        if (!path.isEmpty()) {
            map.insert(QDir::cleanPath(path), icon);
        }
    };

    map.insert(QDir::cleanPath(QDir::homePath()), QStringLiteral("user-home"));
    add(QStandardPaths::DesktopLocation, QStringLiteral("user-desktop"));
    add(QStandardPaths::DocumentsLocation, QStringLiteral("folder-documents"));
    add(QStandardPaths::DownloadLocation, QStringLiteral("folder-download"));
    add(QStandardPaths::PicturesLocation, QStringLiteral("folder-pictures"));
    add(QStandardPaths::MusicLocation, QStringLiteral("folder-music"));
    add(QStandardPaths::MoviesLocation, QStringLiteral("folder-videos"));
    return map;
}

} // namespace

FileItem::FileItem(const QString& absolutePath)
    : m_path(absolutePath)
{
    refresh();
}

FileItem::FileItem(const QFileInfo& fileInfo)
{
    initFromInfo(fileInfo);
}

void FileItem::refresh()
{
    QFileInfo info(m_path);
    initFromInfo(info);
}

void FileItem::initFromInfo(const QFileInfo& info)
{
    m_path = info.absoluteFilePath();
    m_fileName = info.fileName();
    if (m_fileName.isEmpty() && info.isRoot()) {
        m_fileName = QStringLiteral("/");
    }
    m_baseName = info.completeBaseName();
    m_suffix = info.suffix();
    m_exists = info.exists();

    m_isDir = info.isDir();
    m_isFile = info.isFile();
    m_isSymLink = info.isSymLink();
    m_isHidden = info.isHidden();
    m_isReadable = info.isReadable();
    m_isWritable = info.isWritable();
    m_isExecutable = info.isExecutable();

    if (m_isSymLink) {
        m_symLinkTarget = info.symLinkTarget();
    }

    m_size = m_isDir ? 0 : info.size();
    m_formattedSize = m_isDir ? QStringLiteral("--") : formatSize(m_size);

    m_modified = info.lastModified();
    m_created = info.birthTime().isValid() ? info.birthTime() : info.metadataChangeTime();
    m_accessed = info.lastRead();

    // Query POSIX stat/lstat for precise mode, block count, owner, group
    struct stat st;
    QByteArray localPath = QFile::encodeName(m_path);
    if (lstat(localPath.constData(), &st) == 0) {
        m_posixMode = st.st_mode;
        m_diskUsage = static_cast<qint64>(st.st_blocks) * 512;
        m_inode = static_cast<quint64>(st.st_ino);
        m_deviceId = static_cast<quint64>(st.st_dev);
        m_hardLinks = static_cast<quint64>(st.st_nlink);
        m_permissionsString = formatPermissions(st.st_mode);
        m_owner = userName(st.st_uid);
        m_group = groupName(st.st_gid);
    } else {
        m_posixMode = 0;
        m_diskUsage = m_size;
        m_permissionsString = QStringLiteral("---------");
        m_owner = QStringLiteral("unknown");
        m_group = QStringLiteral("unknown");
    }

    if (m_isDir) {
        m_kind = QStringLiteral("Folder");
        m_mimeTypeName = QStringLiteral("inode/directory");
    } else if (m_isSymLink) {
        m_kind = QStringLiteral("Alias / Symlink");
        m_mimeTypeName = QStringLiteral("inode/symlink");
    } else {
        // Extension-only matching avoids opening every file just to list a folder.
        QMimeType mimeType = sharedMimeDatabase().mimeTypeForFile(info, QMimeDatabase::MatchExtension);
        m_mimeTypeName = mimeType.name();
        m_kind = mimeType.comment();
        if (m_kind.isEmpty()) {
            m_kind = m_suffix.isEmpty() ? QStringLiteral("Document") : m_suffix.toUpper() + QStringLiteral(" Document");
        }
    }

    // Tags are loaded lazily: getxattr() is expensive and only visible rows need it.
    m_tags.clear();
    m_tagsLoaded = false;
}

void FileItem::setCalculatedFolderSize(qint64 size, int itemCount)
{
    m_size = size;
    m_formattedSize = (itemCount >= 0)
        ? QStringLiteral("%1 (%2)").arg(formatSize(size)).arg(itemCount == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(itemCount))
        : formatSize(size);
}

const QStringList& FileItem::tags() const
{
    if (!m_tagsLoaded) {
        m_tags = TagManager::instance().tagsForFile(m_path);
        m_tagsLoaded = true;
    }
    return m_tags;
}

QIcon FileItem::icon() const
{
    if (m_isDir) {
        static const QHash<QString, QString> specialDirs = buildSpecialDirIcons();
        const auto special = specialDirs.constFind(QDir::cleanPath(m_path));
        const QString iconName = special != specialDirs.cend()
            ? *special
            : QStringLiteral("folder");

        static QHash<QString, QIcon> directoryIcons;
        const auto cached = directoryIcons.constFind(iconName);
        if (cached != directoryIcons.cend()) {
            return *cached;
        }
        QIcon icon = QIcon::fromTheme(iconName, QIcon::fromTheme(QStringLiteral("folder")));
        directoryIcons.insert(iconName, icon);
        return icon;
    }

    // Cache by suffix: icon lookup (theme + MIME) happens once per extension.
    static QHash<QString, QIcon> fileIcons;
    const QString key = m_suffix.toLower();
    const auto cached = fileIcons.constFind(key);
    if (cached != fileIcons.cend()) {
        return *cached;
    }

    QIcon icon;
    if (!m_suffix.isEmpty()) {
        const QMimeType mime = sharedMimeDatabase().mimeTypeForFile(m_path, QMimeDatabase::MatchExtension);
        icon = QIcon::fromTheme(mime.iconName());
    }
    if (icon.isNull()) {
        static QFileIconProvider provider;
        icon = provider.icon(QFileIconProvider::File);
    }
    fileIcons.insert(key, icon);
    return icon;
}

QString FileItem::formatSize(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("--");
    if (bytes < 1000) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unitIndex = 0;
    double dBytes = static_cast<double>(bytes);
    while (dBytes >= 1000.0 && unitIndex < 5) {
        dBytes /= 1000.0;
        ++unitIndex;
    }
    return QStringLiteral("%1 %2").arg(dBytes, 0, 'f', dBytes < 10.0 ? 1 : 0).arg(QLatin1String(units[unitIndex]));
}

QString FileItem::formatPermissions(mode_t mode)
{
    QString perms;
    perms.reserve(10);
    // Type
    if (S_ISDIR(mode)) perms += 'd';
    else if (S_ISLNK(mode)) perms += 'l';
    else if (S_ISCHR(mode)) perms += 'c';
    else if (S_ISBLK(mode)) perms += 'b';
    else if (S_ISFIFO(mode)) perms += 'p';
    else if (S_ISSOCK(mode)) perms += 's';
    else perms += '-';

    // User
    perms += (mode & S_IRUSR) ? 'r' : '-';
    perms += (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID) perms += (mode & S_IXUSR) ? 's' : 'S';
    else perms += (mode & S_IXUSR) ? 'x' : '-';

    // Group
    perms += (mode & S_IRGRP) ? 'r' : '-';
    perms += (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID) perms += (mode & S_IXGRP) ? 's' : 'S';
    else perms += (mode & S_IXGRP) ? 'x' : '-';

    // Others
    perms += (mode & S_IROTH) ? 'r' : '-';
    perms += (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX) perms += (mode & S_IXOTH) ? 't' : 'T';
    else perms += (mode & S_IXOTH) ? 'x' : '-';

    return perms;
}
