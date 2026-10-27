#include "FileItem.h"
#include "TagManager.h"
#include <QFileIconProvider>
#include <QLocale>
#include <QDir>
#include <QStandardPaths>
#include <cmath>

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
        m_permissionsString = formatPermissions(st.st_mode);

        struct passwd* pw = getpwuid(st.st_uid);
        if (pw && pw->pw_name) {
            m_owner = QString::fromLatin1(pw->pw_name);
        } else {
            m_owner = QString::number(st.st_uid);
        }

        struct group* gr = getgrgid(st.st_gid);
        if (gr && gr->gr_name) {
            m_group = QString::fromLatin1(gr->gr_name);
        } else {
            m_group = QString::number(st.st_gid);
        }
    } else {
        m_posixMode = 0;
        m_diskUsage = m_size;
        m_permissionsString = QStringLiteral("---------");
        m_owner = QStringLiteral("unknown");
        m_group = QStringLiteral("unknown");
    }

    QMimeDatabase mimeDb;
    QMimeType mimeType;
    if (m_isDir) {
        m_kind = QStringLiteral("Folder");
        m_mimeTypeName = QStringLiteral("inode/directory");
    } else if (m_isSymLink) {
        m_kind = QStringLiteral("Alias / Symlink");
        m_mimeTypeName = QStringLiteral("inode/symlink");
    } else {
        mimeType = mimeDb.mimeTypeForFile(info);
        m_mimeTypeName = mimeType.name();
        m_kind = mimeType.comment();
        if (m_kind.isEmpty()) {
            m_kind = m_suffix.isEmpty() ? QStringLiteral("Document") : m_suffix.toUpper() + QStringLiteral(" Document");
        }
    }

    m_tags = TagManager::instance().tagsForFile(m_path);
}

#include <QStandardPaths>

QIcon FileItem::icon() const
{
    QFileInfo info(m_path);
    if (m_isDir) {
        QString clean = QDir::cleanPath(m_path);
        if (clean == QDir::homePath()) {
            return QIcon::fromTheme(QStringLiteral("user-home"),
                   QIcon::fromTheme(QStringLiteral("folder")));
        }
        if (clean == QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)) {
            return QIcon::fromTheme(QStringLiteral("user-desktop"),
                   QIcon::fromTheme(QStringLiteral("folder")));
        }
        if (clean == QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)) {
            return QIcon::fromTheme(QStringLiteral("folder-documents"),
                   QIcon::fromTheme(QStringLiteral("folder")));
        }
        if (clean == QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)) {
            return QIcon::fromTheme(QStringLiteral("folder-download"),
                   QIcon::fromTheme(QStringLiteral("folder")));
        }
        if (clean == QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)) {
            return QIcon::fromTheme(QStringLiteral("folder-pictures"),
                   QIcon::fromTheme(QStringLiteral("folder")));
        }
        if (clean == QStandardPaths::writableLocation(QStandardPaths::MusicLocation)) {
            return QIcon::fromTheme(QStringLiteral("folder-music"),
                   QIcon::fromTheme(QStringLiteral("folder")));
        }
        if (clean == QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)) {
            return QIcon::fromTheme(QStringLiteral("folder-videos"),
                   QIcon::fromTheme(QStringLiteral("folder-movies"),
                   QIcon::fromTheme(QStringLiteral("folder"))));
        }

        QIcon icon = QIcon::fromTheme(QStringLiteral("folder"));
        if (!icon.isNull()) {
            return icon;
        }
    }

    static QFileIconProvider iconProvider;
    return iconProvider.icon(info);
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
