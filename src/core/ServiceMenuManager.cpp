#include "ServiceMenuManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <QUrl>

namespace {

struct ActionGroup {
    QString name;
    QHash<QString, QString> localizedNames;
    QString comment;
    QHash<QString, QString> localizedComments;
    QString icon;
    QString exec;
    QString tryExec;
};

struct ServiceMenuEntry {
    QString type;
    QStringList mimeTypes;
    QStringList serviceTypes;
    QStringList actionOrder;
    QStringList parsedActionOrder;
    QString submenu;
    QHash<QString, QString> localizedSubmenus;
    QString icon;
    int priority{0};
    int minUrls{0};
    int maxUrls{0};
    bool noDisplay{false};
    bool hidden{false};
    QHash<QString, ActionGroup> actions;
};

QStringList splitList(const QString& value, const QString& separators)
{
    QStringList result;
    QString current;
    for (const QChar ch : value) {
        if (separators.contains(ch)) {
            const QString trimmed = current.trimmed();
            if (!trimmed.isEmpty()) {
                result.append(trimmed);
            }
            current.clear();
        } else {
            current += ch;
        }
    }
    const QString trimmed = current.trimmed();
    if (!trimmed.isEmpty()) {
        result.append(trimmed);
    }
    return result;
}

bool readBool(const QString& value)
{
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

QString localizedValue(const QHash<QString, QString>& values, const QString& fallback)
{
    if (values.isEmpty()) {
        return fallback;
    }

    const QString locale = QLocale::system().name();
    const QString language = locale.section(QLatin1Char('_'), 0, 0);
    if (values.contains(locale)) {
        return values.value(locale);
    }
    if (values.contains(language)) {
        return values.value(language);
    }
    return fallback;
}

bool looksLikeMimeType(const QString& value)
{
    if (value.isEmpty() || value.compare(QStringLiteral("KonqPopupMenu/Plugin"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return value.contains(QLatin1Char('/'));
}

ServiceMenuEntry readServiceMenu(const QString& path)
{
    ServiceMenuEntry entry;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }

    QTextStream in(&file);
    enum class Section { None, DesktopEntry, Action } section = Section::None;
    ActionGroup* currentAction = nullptr;
    QString currentActionName;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            const QString header = line.mid(1, line.size() - 2);
            if (header == QStringLiteral("Desktop Entry")) {
                section = Section::DesktopEntry;
                currentAction = nullptr;
            } else if (header.startsWith(QStringLiteral("Desktop Action "))) {
                section = Section::Action;
                currentActionName = header.mid(QStringLiteral("Desktop Action ").size());
                currentAction = &entry.actions[currentActionName];
                if (!entry.parsedActionOrder.contains(currentActionName)) {
                    entry.parsedActionOrder.append(currentActionName);
                }
            } else {
                section = Section::None;
                currentAction = nullptr;
            }
            continue;
        }

        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (section == Section::DesktopEntry) {
            if (key == QStringLiteral("Type")) {
                entry.type = value;
            } else if (key == QStringLiteral("MimeType")) {
                entry.mimeTypes = splitList(value, QStringLiteral(";"));
            } else if (key == QStringLiteral("X-KDE-ServiceTypes") || key == QStringLiteral("ServiceTypes")) {
                entry.serviceTypes = splitList(value, QStringLiteral(";,"));
            } else if (key == QStringLiteral("Actions")) {
                entry.actionOrder = splitList(value, QStringLiteral(";"));
            } else if (key == QStringLiteral("X-KDE-Submenu")) {
                entry.submenu = value;
            } else if (key.startsWith(QStringLiteral("X-KDE-Submenu[")) && key.endsWith(QLatin1Char(']'))) {
                entry.localizedSubmenus.insert(key.mid(14, key.size() - 15), value);
            } else if (key == QStringLiteral("Icon")) {
                entry.icon = value;
            } else if (key == QStringLiteral("X-KDE-Priority")) {
                entry.priority = value.toInt();
            } else if (key == QStringLiteral("X-KDE-MinNumberOfUrls")) {
                entry.minUrls = value.toInt();
            } else if (key == QStringLiteral("X-KDE-MaxNumberOfUrls")) {
                entry.maxUrls = value.toInt();
            } else if (key == QStringLiteral("NoDisplay")) {
                entry.noDisplay = readBool(value);
            } else if (key == QStringLiteral("Hidden")) {
                entry.hidden = readBool(value);
            }
        } else if (section == Section::Action && currentAction) {
            if (key == QStringLiteral("Name")) {
                currentAction->name = value;
            } else if (key.startsWith(QStringLiteral("Name[")) && key.endsWith(QLatin1Char(']'))) {
                currentAction->localizedNames.insert(key.mid(5, key.size() - 6), value);
            } else if (key == QStringLiteral("Comment")) {
                currentAction->comment = value;
            } else if (key.startsWith(QStringLiteral("Comment[")) && key.endsWith(QLatin1Char(']'))) {
                currentAction->localizedComments.insert(key.mid(8, key.size() - 9), value);
            } else if (key == QStringLiteral("Icon")) {
                currentAction->icon = value;
            } else if (key == QStringLiteral("Exec")) {
                currentAction->exec = value;
            } else if (key == QStringLiteral("TryExec")) {
                currentAction->tryExec = value;
            }
        }
    }

    return entry;
}

QSet<QString> mimeCandidates(const QString& path)
{
    QSet<QString> result;
    QMimeDatabase database;
    const QMimeType type = database.mimeTypeForFile(path, QMimeDatabase::MatchDefault);
    if (!type.isValid()) {
        return result;
    }

    result.insert(type.name());
    for (const QString& alias : type.aliases()) {
        result.insert(alias);
    }
    for (const QString& parent : type.parentMimeTypes()) {
        result.insert(parent);
    }
    return result;
}

bool matchesSelection(const ServiceMenuEntry& entry, const QStringList& paths)
{
    if (paths.isEmpty() || entry.mimeTypes.isEmpty()) {
        return false;
    }
    if (entry.minUrls > 0 && paths.size() < entry.minUrls) {
        return false;
    }
    if (entry.maxUrls > 0 && paths.size() > entry.maxUrls) {
        return false;
    }

    for (const QString& path : paths) {
        const bool isDirectory = QFileInfo(path).isDir();
        const QSet<QString> candidates = mimeCandidates(path);
        bool matched = false;
        for (const QString& type : entry.mimeTypes) {
            if (type.compare(QStringLiteral("all/all"), Qt::CaseInsensitive) == 0) {
                matched = true;
                break;
            }
            if (!isDirectory && type.compare(QStringLiteral("all/allfiles"), Qt::CaseInsensitive) == 0) {
                matched = true;
                break;
            }
            if (candidates.contains(type)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

QStringList expandedArguments(const ServiceMenuAction& action, const QStringList& paths)
{
    QStringList result;
    if (paths.isEmpty()) {
        return result;
    }

    const QString firstPath = paths.first();
    const QString firstUrl = QUrl::fromLocalFile(firstPath).toString(QUrl::FullyEncoded);
    const QFileInfo firstInfo(firstPath);
    const QString firstDirectory = firstInfo.isDir() ? firstInfo.absoluteFilePath() : firstInfo.absolutePath();
    const QString firstName = firstInfo.fileName();
    const QString icon = action.icon.isEmpty() ? QStringLiteral("application-x-executable") : action.icon;
    const QString comment = action.comment.isEmpty() ? action.name : action.comment;

    const QStringList tokens = QProcess::splitCommand(action.exec);
    for (const QString& token : tokens) {
        if (token == QStringLiteral("%F") || token == QStringLiteral("%U")) {
            const bool asUrl = token == QStringLiteral("%U");
            for (const QString& path : paths) {
                result.append(asUrl ? QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded) : path);
            }
            continue;
        }

        QString expanded;
        for (qsizetype i = 0; i < token.size(); ++i) {
            const QChar ch = token.at(i);
            if (ch != QLatin1Char('%') || i + 1 >= token.size()) {
                expanded += ch;
                continue;
            }

            const QChar code = token.at(++i);
            switch (code.unicode()) {
            case 'f':
            case 'F':
                expanded += firstPath;
                break;
            case 'u':
            case 'U':
                expanded += firstUrl;
                break;
            case 'd':
            case 'D':
                expanded += firstDirectory;
                break;
            case 'n':
            case 'N':
                expanded += firstName;
                break;
            case 'i':
                expanded += icon;
                break;
            case 'c':
                expanded += comment;
                break;
            case 'k':
                expanded += action.desktopFile;
                break;
            case '%':
                expanded += QLatin1Char('%');
                break;
            default:
                break;
            }
        }

        if (!expanded.isEmpty()) {
            result.append(expanded);
        }
    }

    return result;
}

} // namespace

QStringList ServiceMenuManager::searchPaths()
{
    QStringList result;
    const QStringList dataDirectories =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString& dataDirectory : dataDirectories) {
        result.append(dataDirectory + QStringLiteral("/kio/servicemenus"));
        result.append(dataDirectory + QStringLiteral("/kservices5/ServiceMenus"));
    }

    // Legacy locations may not be present in XDG_DATA_DIRS on every system.
    result.append(QStringLiteral("/usr/local/share/kio/servicemenus"));
    result.append(QStringLiteral("/usr/share/kio/servicemenus"));
    result.append(QStringLiteral("/usr/local/share/kservices5/ServiceMenus"));
    result.append(QStringLiteral("/usr/share/kservices5/ServiceMenus"));
    result.removeDuplicates();
    return result;
}

QVector<ServiceMenuAction> ServiceMenuManager::actionsFor(
    const QStringList& paths, const QStringList& searchDirectories)
{
    QVector<ServiceMenuAction> result;
    if (paths.isEmpty()) {
        return result;
    }

    const QStringList directories = searchDirectories.isEmpty() ? searchPaths() : searchDirectories;
    QSet<QString> seenDesktopFiles;

    for (const QString& directoryPath : directories) {
        const QDir directory(directoryPath);
        if (!directory.exists()) {
            continue;
        }

        const QFileInfoList files = directory.entryInfoList(
            {QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
        for (const QFileInfo& fileInfo : files) {
            if (seenDesktopFiles.contains(fileInfo.fileName())) {
                continue;
            }
            seenDesktopFiles.insert(fileInfo.fileName());

            const ServiceMenuEntry entry = readServiceMenu(fileInfo.absoluteFilePath());
            if (entry.hidden || entry.noDisplay) {
                continue;
            }
            if (!entry.type.isEmpty() && entry.type.compare(QStringLiteral("Service"), Qt::CaseInsensitive) != 0) {
                continue;
            }

            QStringList mimeTypes = entry.mimeTypes;
            for (const QString& serviceType : entry.serviceTypes) {
                if (looksLikeMimeType(serviceType)) {
                    mimeTypes.append(serviceType);
                }
            }
            mimeTypes.removeDuplicates();
            if (mimeTypes.isEmpty()) {
                continue;
            }

            ServiceMenuEntry filtered = entry;
            filtered.mimeTypes = mimeTypes;
            if (!matchesSelection(filtered, paths)) {
                continue;
            }

            const QStringList actionNames = entry.actionOrder.isEmpty()
                ? entry.parsedActionOrder : entry.actionOrder;
            for (const QString& actionName : actionNames) {
                if (!entry.actions.contains(actionName)) {
                    continue;
                }
                const ActionGroup& group = entry.actions.value(actionName);
                if (group.exec.isEmpty()) {
                    continue;
                }
                if (!group.tryExec.isEmpty()
                    && QStandardPaths::findExecutable(group.tryExec).isEmpty()) {
                    continue;
                }

                ServiceMenuAction action;
                action.name = localizedValue(group.localizedNames, group.name);
                if (action.name.isEmpty()) {
                    action.name = actionName;
                }
                action.comment = localizedValue(group.localizedComments, group.comment);
                action.icon = group.icon.isEmpty() ? entry.icon : group.icon;
                action.exec = group.exec;
                action.submenu = localizedValue(entry.localizedSubmenus, entry.submenu);
                action.desktopFile = fileInfo.absoluteFilePath();
                action.actionName = actionName;
                action.priority = entry.priority;
                result.append(action);
            }
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const ServiceMenuAction& a, const ServiceMenuAction& b) {
        if (a.priority != b.priority) {
            return a.priority > b.priority;
        }
        const int submenuCompare = QString::compare(a.submenu, b.submenu, Qt::CaseInsensitive);
        if (submenuCompare != 0) {
            return submenuCompare < 0;
        }
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    return result;
}

bool ServiceMenuManager::launch(const ServiceMenuAction& action, const QStringList& paths)
{
    if (action.exec.isEmpty() || paths.isEmpty()) {
        return false;
    }

    QStringList arguments = expandedArguments(action, paths);
    if (arguments.isEmpty()) {
        return false;
    }

    const QString program = arguments.takeFirst();
    const QFileInfo firstInfo(paths.first());
    const QString workingDirectory = firstInfo.isDir()
        ? firstInfo.absoluteFilePath() : firstInfo.absolutePath();
    return QProcess::startDetached(program, arguments, workingDirectory);
}
