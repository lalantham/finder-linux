#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QIcon>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QList>

struct TagInfo {
    QString name;
    QColor color;
};

class TagManager : public QObject {
    Q_OBJECT

public:
    static TagManager& instance();

    // Standard 7 macOS Finder colors
    static const QList<TagInfo>& standardTags();
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

    // Queries
    QStringList filesForTag(const QString& tag);
    int tagCount(const QString& tag);

signals:
    void tagsChanged(const QString& path, const QStringList& tags);

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
    QString m_registryPath;
};
