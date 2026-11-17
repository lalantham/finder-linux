#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>

class QTimer;

// Persists Finder-style metadata that the filesystem cannot provide directly:
// the first time an item was seen (Date Added) and the last time it was opened.
class FileMetadataManager : public QObject {
    Q_OBJECT

public:
    static FileMetadataManager& instance();
    static void setStoragePathForTesting(const QString& path);

    [[nodiscard]] QDateTime dateAdded(const QString& path) const;
    [[nodiscard]] QDateTime lastOpened(const QString& path) const;

    void registerPaths(const QStringList& paths);
    void markOpened(const QString& path);
    void renamePath(const QString& oldPath, const QString& newPath);
    void forgetPath(const QString& path);
    void forgetPaths(const QStringList& paths);
    void save();

    // Drops entries whose files no longer exist, then trims the oldest entries
    // if the store still exceeds `maxEntries` (0 = no cap). Returns how many
    // entries were removed. Safe to call periodically; it only touches memory
    // plus one stat() per entry.
    int pruneMissingEntries(int maxEntries = 0);

private:
    FileMetadataManager();
    ~FileMetadataManager() override;

    void load();
    void scheduleSave();
    static QJsonObject buildSortedObject(const QHash<QString, qint64>& source);

    QString m_storagePath;
    QHash<QString, qint64> m_added;
    QHash<QString, qint64> m_opened;
    QTimer* m_saveTimer{nullptr};
};
