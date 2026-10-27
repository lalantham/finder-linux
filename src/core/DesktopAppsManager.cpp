#include "DesktopAppsManager.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>

QStringList DesktopAppsManager::desktopSearchPaths()
{
    QStringList paths;
    QString userApps = QDir::homePath() + QStringLiteral("/.local/share/applications");
    if (QDir(userApps).exists()) paths.append(userApps);

    paths.append(QStringLiteral("/usr/local/share/applications"));
    paths.append(QStringLiteral("/usr/share/applications"));
    return paths;
}

QString DesktopAppsManager::defaultAppIdForMime(const QString& mimeType)
{
    if (mimeType.isEmpty()) return {};

    QProcess proc;
    proc.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mimeType});
    if (proc.waitForFinished(1000)) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            return out;
        }
    }

    // Fallback: check ~/.config/mimeapps.list
    QString mimeAppsPath = QDir::homePath() + QStringLiteral("/.config/mimeapps.list");
    if (QFile::exists(mimeAppsPath)) {
        QSettings settings(mimeAppsPath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Default Applications"));
        QString val = settings.value(mimeType).toString();
        settings.endGroup();
        if (!val.isEmpty()) {
            return val.split(QLatin1Char(';')).first();
        }
    }

    return {};
}

QVector<AppEntry> DesktopAppsManager::appsForMime(const QString& mimeType)
{
    QVector<AppEntry> list;
    if (mimeType.isEmpty()) return list;

    QString defaultId = defaultAppIdForMime(mimeType);
    QSet<QString> seenIds;

    for (const auto& dirPath : desktopSearchPaths()) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        const auto entries = dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const auto& fi : entries) {
            QString desktopId = fi.fileName();
            if (seenIds.contains(desktopId)) continue;

            QSettings desktopFile(fi.absoluteFilePath(), QSettings::IniFormat);
            desktopFile.beginGroup(QStringLiteral("Desktop Entry"));

            bool noDisplay = desktopFile.value(QStringLiteral("NoDisplay"), false).toBool();
            if (noDisplay) {
                desktopFile.endGroup();
                continue;
            }

            QString mimesStr = desktopFile.value(QStringLiteral("MimeType")).toString();
            QStringList mimeList = mimesStr.split(QLatin1Char(';'), Qt::SkipEmptyParts);

            bool matches = false;
            for (const auto& m : mimeList) {
                if (m.trimmed() == mimeType) {
                    matches = true;
                    break;
                }
            }

            if (matches) {
                seenIds.insert(desktopId);

                AppEntry entry;
                entry.id = desktopId;
                entry.name = desktopFile.value(QStringLiteral("Name")).toString();
                if (entry.name.isEmpty()) entry.name = fi.completeBaseName();
                entry.exec = desktopFile.value(QStringLiteral("Exec")).toString();
                entry.iconName = desktopFile.value(QStringLiteral("Icon")).toString();
                entry.icon = QIcon::fromTheme(entry.iconName, QIcon::fromTheme(QStringLiteral("application-x-executable")));
                entry.isDefault = (desktopId == defaultId);

                list.append(entry);
            }

            desktopFile.endGroup();
        }
    }

    // Put default app first, then sort alphabetically by name
    std::sort(list.begin(), list.end(), [](const AppEntry& a, const AppEntry& b) {
        if (a.isDefault != b.isDefault) return a.isDefault;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    return list;
}

QVector<AppEntry> DesktopAppsManager::allDesktopApps()
{
    QVector<AppEntry> list;
    QSet<QString> seenIds;

    for (const auto& dirPath : desktopSearchPaths()) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        const auto entries = dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const auto& fi : entries) {
            QString desktopId = fi.fileName();
            if (seenIds.contains(desktopId)) continue;

            QSettings desktopFile(fi.absoluteFilePath(), QSettings::IniFormat);
            desktopFile.beginGroup(QStringLiteral("Desktop Entry"));

            bool noDisplay = desktopFile.value(QStringLiteral("NoDisplay"), false).toBool();
            QString type = desktopFile.value(QStringLiteral("Type")).toString();
            if (noDisplay || type != QStringLiteral("Application")) {
                desktopFile.endGroup();
                continue;
            }

            seenIds.insert(desktopId);

            AppEntry entry;
            entry.id = desktopId;
            entry.name = desktopFile.value(QStringLiteral("Name")).toString();
            if (entry.name.isEmpty()) entry.name = fi.completeBaseName();
            entry.exec = desktopFile.value(QStringLiteral("Exec")).toString();
            entry.iconName = desktopFile.value(QStringLiteral("Icon")).toString();
            entry.icon = QIcon::fromTheme(entry.iconName, QIcon::fromTheme(QStringLiteral("application-x-executable")));

            list.append(entry);
            desktopFile.endGroup();
        }
    }

    std::sort(list.begin(), list.end(), [](const AppEntry& a, const AppEntry& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    return list;
}

bool DesktopAppsManager::setDefaultAppForMime(const QString& mimeType, const QString& desktopFileId)
{
    if (mimeType.isEmpty() || desktopFileId.isEmpty()) return false;

    // Use xdg-mime tool first
    QProcess proc;
    proc.start(QStringLiteral("xdg-mime"), {QStringLiteral("default"), desktopFileId, mimeType});
    if (proc.waitForFinished(2000) && proc.exitCode() == 0) {
        return true;
    }

    // Direct write to ~/.config/mimeapps.list
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir().mkpath(configDir);
    QString mimeAppsPath = configDir + QStringLiteral("/mimeapps.list");

    QSettings settings(mimeAppsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Default Applications"));
    settings.setValue(mimeType, desktopFileId);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Added Associations"));
    QString current = settings.value(mimeType).toString();
    QStringList currentList = current.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    if (!currentList.contains(desktopFileId)) {
        currentList.prepend(desktopFileId);
        settings.setValue(mimeType, currentList.join(QLatin1Char(';')) + QStringLiteral(";"));
    }
    settings.endGroup();

    settings.sync();
    return true;
}

bool DesktopAppsManager::launchApp(const QString& desktopFileId, const QString& filePath)
{
    if (desktopFileId.isEmpty()) return false;

    // 1. Try gtk-launch
    if (QProcess::startDetached(QStringLiteral("gtk-launch"), {desktopFileId, filePath})) {
        return true;
    }

    // 2. Try gio launch
    for (const auto& dirPath : desktopSearchPaths()) {
        QString fullPath = dirPath + QStringLiteral("/") + desktopFileId;
        if (QFile::exists(fullPath)) {
            if (QProcess::startDetached(QStringLiteral("gio"), {QStringLiteral("launch"), fullPath, filePath})) {
                return true;
            }
        }
    }

    // 3. Fallback: Parse Exec from desktop file and launch
    for (const auto& dirPath : desktopSearchPaths()) {
        QString fullPath = dirPath + QStringLiteral("/") + desktopFileId;
        if (QFile::exists(fullPath)) {
            QSettings desktopFile(fullPath, QSettings::IniFormat);
            desktopFile.beginGroup(QStringLiteral("Desktop Entry"));
            QString exec = desktopFile.value(QStringLiteral("Exec")).toString();
            desktopFile.endGroup();

            if (!exec.isEmpty()) {
                // Remove field codes (%f, %F, %u, %U)
                exec.remove(QStringLiteral("%f"));
                exec.remove(QStringLiteral("%F"));
                exec.remove(QStringLiteral("%u"));
                exec.remove(QStringLiteral("%U"));
                QStringList parts = QProcess::splitCommand(exec.trimmed());
                if (!parts.isEmpty()) {
                    QString program = parts.takeFirst();
                    if (!filePath.isEmpty()) {
                        parts.append(filePath);
                    }
                    return QProcess::startDetached(program, parts);
                }
            }
        }
    }

    return false;
}
