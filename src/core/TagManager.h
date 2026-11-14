#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

struct TagInfo {
    QString name;
    QColor color;
};

class TagManager : public QObject {
    Q_OBJECT

public:
    static TagManager& instance();

    // Standard tag colors
    static const QList<TagInfo>& standardTags();
    static QList<TagInfo> allTags();
    static QColor colorForTag(const QString& tag);
    static QIcon tagDotIcon(const QString& tag, int size = 16);
    static QIcon tagDotIcon(const QColor& color, int size = 16);

    // Tag inspection
    QStringList tagsForFile(const QString& path);
    bool hasTag(const QString& path, const QString& tag);

    // Tag mutations
    bool setTagsForFile(const QString& path, const QStringList& tags);
    bool addTag(const QString& path, const QString& tag);
    bool removeTag(const QString& path, const QString& tag);
    bool toggleTag(const QString& path, const QString& tag);
    bool clearTags(const QString& path);

    // Custom tag definitions
    [[nodiscard]] QList<TagInfo> customTags() const;
    bool addCustomTag(const QString& name, const QColor& color, QString* error = nullptr);
    bool renameTag(const QString& oldName, const QString& newName, QString* error = nullptr);
    bool removeTagDefinition(const QString& name, QString* error = nullptr);

    // Queries
    QStringList filesForTag(const QString& tag);
    int tagCount(const QString& tag);

signals:
    void tagsChanged(const QString& path, const QStringList& tags);
    void tagDefinitionsChanged();

private:
    TagManager();
    ~TagManager() override;

    void loadRegistry();
    void saveRegistry();

    QStringList readXattr(const QString& path);
    bool writeXattr(const QString& path, const QStringList& tags);

    mutable QMutex m_mutex;
    QMap<QString, QStringList> m_fileTags;
    QMap<QString, QSet<QString>> m_tagFiles;
    QList<TagInfo> m_customTags;
    QString m_registryPath;
};
