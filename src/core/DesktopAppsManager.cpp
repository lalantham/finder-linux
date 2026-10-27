#include "DesktopAppsManager.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>
#include <QTextStream>
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

    // 1. Query xdg-mime query default <mimeType>
    QProcess proc;
    proc.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mimeType});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            return out;
        }
    }

    // 2. Query gio mime <mimeType>
    QProcess gioProc;
    gioProc.start(QStringLiteral("gio"), {QStringLiteral("mime"), mimeType});
    if (gioProc.waitForFinished(1000) && gioProc.exitCode() == 0) {
        QString out = QString::fromUtf8(gioProc.readAllStandardOutput());
        static QRegularExpression re(QStringLiteral("Default application for [^:]+:\\s*(\\S+)"));
        auto match = re.match(out);
        if (match.hasMatch()) {
            return match.captured(1).trimmed();
        }
    }

    // 3. Fallback: Parse ~/.config/mimeapps.list directly as raw text (immune to QSettings slash bugs)
    QString mimeAppsPath = QDir::homePath() + QStringLiteral("/.config/mimeapps.list");
    if (QFile::exists(mimeAppsPath)) {
        QFile file(mimeAppsPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            bool inDefaults = false;
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inDefaults = (line == QStringLiteral("[Default Applications]"));
                } else if (inDefaults && line.startsWith(mimeType + QLatin1Char('='))) {
                    QString val = line.mid(mimeType.length() + 1).trimmed();
                    return val.split(QLatin1Char(';')).first();
                }
            }
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

    // 1. Run xdg-mime default <desktopFileId> <mimeType>
    QProcess xdgProc;
    xdgProc.start(QStringLiteral("xdg-mime"), {QStringLiteral("default"), desktopFileId, mimeType});
    xdgProc.waitForFinished(2000);

    // 2. Run gio mime <mimeType> <desktopFileId>
    QProcess gioProc;
    gioProc.start(QStringLiteral("gio"), {QStringLiteral("mime"), mimeType, desktopFileId});
    gioProc.waitForFinished(2000);

    // 3. Directly update ~/.config/mimeapps.list as raw text (safe from QSettings escaping bugs)
    QString configDir = QDir::homePath() + QStringLiteral("/.config");
    QDir().mkpath(configDir);
    QString mimePath = configDir + QStringLiteral("/mimeapps.list");

    QStringList lines;
    if (QFile::exists(mimePath)) {
        QFile f(mimePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                lines.append(in.readLine());
            }
        }
    }

    auto updateSection = [&](const QString& sectionHeader, const QString& key, const QString& value, bool appendSemis) {
        int sectionIdx = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].trimmed() == sectionHeader) {
                sectionIdx = i;
                break;
            }
        }

        if (sectionIdx == -1) {
            lines.append(QString());
            lines.append(sectionHeader);
            sectionIdx = static_cast<int>(lines.size()) - 1;
        }

        int keyIdx = -1;
        for (int i = sectionIdx + 1; i < lines.size(); ++i) {
            if (lines[i].trimmed().startsWith(QLatin1Char('['))) {
                break;
            }
            if (lines[i].trimmed().startsWith(key + QLatin1Char('='))) {
                keyIdx = i;
                break;
            }
        }

        QString newLine = key + QLatin1Char('=') + value;
        if (appendSemis && !newLine.endsWith(QLatin1Char(';'))) {
            newLine += QLatin1Char(';');
        }

        if (keyIdx != -1) {
            if (appendSemis) {
                QString cur = lines[keyIdx].mid(key.length() + 1).trimmed();
                QStringList parts = cur.split(QLatin1Char(';'), Qt::SkipEmptyParts);
                parts.removeAll(value);
                parts.prepend(value);
                lines[keyIdx] = key + QLatin1Char('=') + parts.join(QLatin1Char(';')) + QLatin1Char(';');
            } else {
                lines[keyIdx] = newLine;
            }
        } else {
            lines.insert(sectionIdx + 1, newLine);
        }
    };

    updateSection(QStringLiteral("[Default Applications]"), mimeType, desktopFileId, false);
    updateSection(QStringLiteral("[Added Associations]"), mimeType, desktopFileId, true);

    QFile outFile(mimePath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&outFile);
        for (const auto& l : lines) {
            out << l << QLatin1Char('\n');
        }
        outFile.close();
    }

    // 4. Update KDE Plasma sycoca database cache
    QProcess::startDetached(QStringLiteral("kbuildsycoca6"), {QStringLiteral("--noincremental")});

    return true;
}

bool DesktopAppsManager::launchApp(const QString& desktopFileId, const QString& filePath)
{
    if (desktopFileId.isEmpty()) return false;

    // Find full path of desktop file
    QString fullDesktopPath;
    for (const auto& dirPath : desktopSearchPaths()) {
        QString p = dirPath + QStringLiteral("/") + desktopFileId;
        if (QFile::exists(p)) {
            fullDesktopPath = p;
            break;
        }
    }

    // 1. Try gio launch <fullDesktopPath> [filePath]
    if (!fullDesktopPath.isEmpty()) {
        QStringList gioArgs = {QStringLiteral("launch"), fullDesktopPath};
        if (!filePath.isEmpty()) {
            gioArgs.append(filePath);
        }
        if (QProcess::startDetached(QStringLiteral("gio"), gioArgs)) {
            return true;
        }
    }

    // 2. Try gtk-launch <desktopFileId> [filePath]
    QStringList gtkArgs = {desktopFileId};
    if (!filePath.isEmpty()) {
        gtkArgs.append(filePath);
    }
    if (QProcess::startDetached(QStringLiteral("gtk-launch"), gtkArgs)) {
        return true;
    }

    // 3. Fallback: Parse Exec from desktop file and launch
    if (!fullDesktopPath.isEmpty()) {
        QFile file(fullDesktopPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString execLine;
            bool inDesktopEntry = false;
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inDesktopEntry = (line == QStringLiteral("[Desktop Entry]"));
                } else if (inDesktopEntry && line.startsWith(QStringLiteral("Exec="))) {
                    execLine = line.mid(5).trimmed();
                    break;
                }
            }

            if (!execLine.isEmpty()) {
                bool hadFileArg = false;
                if (execLine.contains(QStringLiteral("%f")) || execLine.contains(QStringLiteral("%F")) ||
                    execLine.contains(QStringLiteral("%u")) || execLine.contains(QStringLiteral("%U"))) {
                    hadFileArg = true;
                    execLine.replace(QStringLiteral("%f"), filePath);
                    execLine.replace(QStringLiteral("%F"), filePath);
                    execLine.replace(QStringLiteral("%u"), filePath);
                    execLine.replace(QStringLiteral("%U"), filePath);
                }
                execLine.remove(QRegularExpression(QStringLiteral("%[a-zA-Z]")));

                QStringList parts = QProcess::splitCommand(execLine.trimmed());
                if (!parts.isEmpty()) {
                    QString program = parts.takeFirst();
                    if (!hadFileArg && !filePath.isEmpty()) {
                        parts.append(filePath);
                    }
                    return QProcess::startDetached(program, parts);
                }
            }
        }
    }

    return false;
}

bool DesktopAppsManager::launchFile(const QString& filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) return false;

    QFileInfo fi(filePath);
    if (fi.isDir()) {
        return false;
    }

    QMimeDatabase mimeDb;
    QString mimeType = mimeDb.mimeTypeForFile(filePath, QMimeDatabase::MatchDefault).name();

    // 1. Direct default lookup
    QString defaultId = defaultAppIdForMime(mimeType);
    if (!defaultId.isEmpty()) {
        if (launchApp(defaultId, filePath)) {
            return true;
        }
    }

    // 2. Apps registered for this MIME type
    auto apps = appsForMime(mimeType);
    if (!apps.isEmpty()) {
        if (launchApp(apps.first().id, filePath)) {
            return true;
        }
    }

    // 3. Fallback: gio open
    if (QProcess::startDetached(QStringLiteral("gio"), {QStringLiteral("open"), filePath})) {
        return true;
    }

    // 4. Fallback: xdg-open
    if (QProcess::startDetached(QStringLiteral("xdg-open"), {filePath})) {
        return true;
    }

    // 5. Final fallback
    return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}
