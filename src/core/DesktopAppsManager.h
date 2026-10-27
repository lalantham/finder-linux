#pragma once

#include <QString>
#include <QVector>
#include <QIcon>

struct AppEntry {
    QString id;       // e.g. "org.kde.kate.desktop"
    QString name;     // e.g. "Kate"
    QString exec;     // e.g. "kate %U"
    QString iconName; // e.g. "kate"
    QIcon icon;
    bool isDefault{false};
};

class DesktopAppsManager {
public:
    static QVector<AppEntry> appsForMime(const QString& mimeType);
    static QString defaultAppIdForMime(const QString& mimeType);
    static bool setDefaultAppForMime(const QString& mimeType, const QString& desktopFileId);
    static bool launchApp(const QString& desktopFileId, const QString& filePath);
    static bool launchFile(const QString& filePath);
    static QVector<AppEntry> allDesktopApps();

private:
    static QStringList desktopSearchPaths();
};
