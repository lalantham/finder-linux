#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

struct RecentFile {
    QString path;
    QString mimeType;
    QDateTime modified;
};

// Reads and updates the freedesktop.org recently-used.xbel database.
class RecentFilesManager {
public:
    static QList<RecentFile> list(int limit = 20);
    static bool add(const QString& path, const QString& mimeType = QString(),
                    const QString& appName = QStringLiteral("Finder"));
    static void clear();
    static QString storagePath();
    static void setStoragePathForTesting(const QString& path);
};
