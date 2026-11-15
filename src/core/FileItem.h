#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QIcon>
#include <QMimeDatabase>
#include <QMimeType>
#include <QString>
#include <grp.h>
#include <memory>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

class FileItem {
public:
    FileItem() = default;
    explicit FileItem(const QString& absolutePath);
    explicit FileItem(const QFileInfo& fileInfo);

    void refresh();
    void setCalculatedFolderSize(qint64 size, int itemCount = -1);
    void setRelevance(int relevance) noexcept {
        m_relevance = relevance;
    }
    [[nodiscard]] int relevance() const noexcept {
        return m_relevance;
    }

    [[nodiscard]] const QString& path() const noexcept {
        return m_path;
    }
    [[nodiscard]] const QString& fileName() const noexcept {
        return m_fileName;
    }
    [[nodiscard]] const QString& baseName() const noexcept {
        return m_baseName;
    }
    [[nodiscard]] const QString& suffix() const noexcept {
        return m_suffix;
    }
    [[nodiscard]] qint64 size() const noexcept {
        return m_size;
    }
    [[nodiscard]] qint64 diskUsage() const noexcept {
        return m_diskUsage;
    }
    [[nodiscard]] quint64 inode() const noexcept {
        return m_inode;
    }
    [[nodiscard]] quint64 deviceId() const noexcept {
        return m_deviceId;
    }
    [[nodiscard]] quint64 hardLinks() const noexcept {
        return m_hardLinks;
    }
    [[nodiscard]] const QString& formattedSize() const noexcept {
        return m_formattedSize;
    }
    [[nodiscard]] const QDateTime& modified() const noexcept {
        return m_modified;
    }
    [[nodiscard]] const QDateTime& created() const noexcept {
        return m_created;
    }
    [[nodiscard]] const QDateTime& accessed() const noexcept {
        return m_accessed;
    }
    [[nodiscard]] const QString& kind() const noexcept {
        return m_kind;
    }
    [[nodiscard]] const QString& mimeTypeName() const noexcept {
        return m_mimeTypeName;
    }

    [[nodiscard]] bool isDir() const noexcept {
        return m_isDir;
    }
    [[nodiscard]] bool isFile() const noexcept {
        return m_isFile;
    }
    [[nodiscard]] bool isSymLink() const noexcept {
        return m_isSymLink;
    }
    [[nodiscard]] bool isHidden() const noexcept {
        return m_isHidden;
    }
    [[nodiscard]] bool isReadable() const noexcept {
        return m_isReadable;
    }
    [[nodiscard]] bool isWritable() const noexcept {
        return m_isWritable;
    }
    [[nodiscard]] bool isExecutable() const noexcept {
        return m_isExecutable;
    }
    [[nodiscard]] bool exists() const noexcept {
        return m_exists;
    }

    [[nodiscard]] const QString& symLinkTarget() const noexcept {
        return m_symLinkTarget;
    }
    [[nodiscard]] const QString& permissionsString() const noexcept {
        return m_permissionsString;
    }
    [[nodiscard]] mode_t posixMode() const noexcept {
        return m_posixMode;
    }
    [[nodiscard]] const QString& owner() const noexcept {
        return m_owner;
    }
    [[nodiscard]] const QString& group() const noexcept {
        return m_group;
    }
    [[nodiscard]] const QStringList& tags() const;
    [[nodiscard]] QString comment() const;
    bool setComment(const QString& comment);
    [[nodiscard]] QString version() const;
    bool setVersion(const QString& version);

    [[nodiscard]] QIcon icon() const;

    static QString formatSize(qint64 bytes);
    static QString formatPermissions(mode_t mode);

private:
    void initFromInfo(const QFileInfo& info);

    QString m_path;
    QString m_fileName;
    QString m_baseName;
    QString m_suffix;
    qint64 m_size{0};
    int m_relevance{0};
    qint64 m_diskUsage{0};
    quint64 m_inode{0};
    quint64 m_deviceId{0};
    quint64 m_hardLinks{0};
    QString m_formattedSize;
    QDateTime m_modified;
    QDateTime m_created;
    QDateTime m_accessed;
    QString m_kind;
    QString m_mimeTypeName;
    mutable QStringList m_tags;
    mutable bool m_tagsLoaded{false};
    mutable QString m_comment;
    mutable bool m_commentLoaded{false};
    mutable QString m_version;
    mutable bool m_versionLoaded{false};

    bool m_isDir{false};
    bool m_isFile{false};
    bool m_isSymLink{false};
    bool m_isHidden{false};
    bool m_isReadable{false};
    bool m_isWritable{false};
    bool m_isExecutable{false};
    bool m_exists{false};

    QString m_symLinkTarget;
    QString m_permissionsString;
    mode_t m_posixMode{0};
    QString m_owner;
    QString m_group;
};
