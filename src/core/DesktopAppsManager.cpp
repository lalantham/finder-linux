#include "DesktopAppsManager.h"
#include "RecentFilesManager.h"
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QUrl>
#include <algorithm>
#include <functional>

namespace {
constexpr qint64 AppsCacheTtlMs = 5000;

struct AppsCacheEntry {
    qint64 timestamp{0};
    QVector<AppEntry> apps;
};

QHash<QString, AppsCacheEntry> s_appsForMimeCache;
qint64 s_allAppsTimestamp{0};
QVector<AppEntry> s_allAppsCache;
struct DefaultAppCacheEntry {
    qint64 timestamp{0};
    QString id;
};
QHash<QString, DefaultAppCacheEntry> s_defaultAppCache;

struct DesktopEntry {
    QString name;
    QString exec;
    QString icon;
    QString mimeType;
    QString type;
    QString path; // working directory (Path=)
    QString tryExec;
    QStringList onlyShowIn;
    QStringList notShowIn;
    bool noDisplay{false};
    bool hidden{false};
    bool terminal{false};
};

QStringList splitList(const QString& value) {
    QStringList result;
    const QStringList parts = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

bool readBool(const QString& value) {
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

// QSettings cannot be used here: its INI parser treats ';' as a comment
// delimiter, which truncates semicolon-separated lists like MimeType.
DesktopEntry readDesktopEntry(const QString& path) {
    DesktopEntry entry;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entry;

    QHash<QString, QString> localizedNames;
    QTextStream in(&file);
    bool inDesktopEntry = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntry = (line == QStringLiteral("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntry)
            continue;

        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;

        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (key == QStringLiteral("Name")) {
            entry.name = value;
        } else if (key.startsWith(QStringLiteral("Name[")) && key.endsWith(QLatin1Char(']'))) {
            localizedNames.insert(key.mid(5, key.size() - 6), value);
        } else if (key == QStringLiteral("Exec")) {
            entry.exec = value;
        } else if (key == QStringLiteral("Icon")) {
            entry.icon = value;
        } else if (key == QStringLiteral("MimeType")) {
            entry.mimeType = value;
        } else if (key == QStringLiteral("Type")) {
            entry.type = value;
        } else if (key == QStringLiteral("Path")) {
            entry.path = value;
        } else if (key == QStringLiteral("TryExec")) {
            entry.tryExec = value;
        } else if (key == QStringLiteral("OnlyShowIn")) {
            entry.onlyShowIn = splitList(value);
        } else if (key == QStringLiteral("NotShowIn")) {
            entry.notShowIn = splitList(value);
        } else if (key == QStringLiteral("NoDisplay")) {
            entry.noDisplay = readBool(value);
        } else if (key == QStringLiteral("Hidden")) {
            entry.hidden = readBool(value);
        } else if (key == QStringLiteral("Terminal")) {
            entry.terminal = readBool(value);
        }
    }

    if (!localizedNames.isEmpty()) {
        const QString locale = QLocale::system().name();                 // e.g. "en_US"
        const QString language = locale.section(QLatin1Char('_'), 0, 0); // "en"
        if (localizedNames.contains(locale)) {
            entry.name = localizedNames.value(locale);
        } else if (localizedNames.contains(language)) {
            entry.name = localizedNames.value(language);
        }
    }

    return entry;
}

QStringList currentDesktops() {
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

QStringList applicationDirs() {
    QStringList paths = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    // Flatpak and Snap export their desktop entries outside XDG_DATA_DIRS.
    const QString home = QDir::homePath();
    paths << home + QStringLiteral("/.local/share/flatpak/exports/share/applications");
    paths << QStringLiteral("/var/lib/flatpak/exports/share/applications");
    paths << QStringLiteral("/var/lib/snapd/desktop/applications");

    QStringList result;
    for (const auto& path : paths) {
        if (!path.isEmpty() && QDir(path).exists() && !result.contains(path)) {
            result.append(path);
        }
    }
    return result;
}

QString desktopFileForId(const QString& desktopId) {
    if (desktopId.isEmpty() || desktopId.contains(QLatin1Char('/')))
        return {};
    for (const auto& dirPath : applicationDirs()) {
        const QString candidate = dirPath + QLatin1Char('/') + desktopId;
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool isListable(const DesktopEntry& entry, bool explicitlyAssociated) {
    if (entry.hidden)
        return false;
    if (!entry.type.isEmpty() && entry.type != QStringLiteral("Application"))
        return false;
    if (entry.noDisplay && !explicitlyAssociated)
        return false;

    if (!entry.tryExec.isEmpty() && QStandardPaths::findExecutable(entry.tryExec).isEmpty()) {
        return false;
    }

    const QStringList desktops = currentDesktops();
    if (!desktops.isEmpty()) {
        if (!entry.onlyShowIn.isEmpty()) {
            bool match = false;
            for (const auto& current : desktops) {
                for (const auto& allowed : entry.onlyShowIn) {
                    if (allowed.compare(current, Qt::CaseInsensitive) == 0) {
                        match = true;
                        break;
                    }
                }
                if (match)
                    break;
            }
            if (!match)
                return false;
        }
        for (const auto& current : desktops) {
            for (const auto& denied : entry.notShowIn) {
                if (denied.compare(current, Qt::CaseInsensitive) == 0) {
                    return false;
                }
            }
        }
    }

    return true;
}

// mimeinfo.cache is generated by update-desktop-database and is the same index
// GIO/KDE use to find handlers. Reading it avoids parsing hundreds of desktop
// files for every "Open With" query.
struct MimeCache {
    QHash<QString, QStringList> appsByMime;
    bool anyCacheFound{false};
};

MimeCache readMimeInfoCache() {
    MimeCache cache;
    for (const auto& dirPath : applicationDirs()) {
        QFile file(dirPath + QStringLiteral("/mimeinfo.cache"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        cache.anyCacheFound = true;

        QTextStream in(&file);
        bool inCache = false;
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            if (line.startsWith(QLatin1Char('['))) {
                inCache = (line == QStringLiteral("[MIME Cache]"));
                continue;
            }
            if (!inCache)
                continue;

            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString mime = line.left(eq).trimmed();
            if (mime.isEmpty())
                continue;

            QStringList& ids = cache.appsByMime[mime];
            for (const auto& id : splitList(line.mid(eq + 1))) {
                if (!ids.contains(id)) {
                    ids.append(id);
                }
            }
        }
    }
    return cache;
}

QStringList mimeAppsListFiles() {
    QStringList configDirs;
    const QString userConfig =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!userConfig.isEmpty()) {
        configDirs.append(userConfig);
    }
    const QString envConfigDirs = qEnvironmentVariable("XDG_CONFIG_DIRS");
    if (!envConfigDirs.isEmpty()) {
        configDirs.append(envConfigDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts));
    } else {
        configDirs.append(QStringLiteral("/etc/xdg"));
    }

    QStringList dataDirs;
    const QString userData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!userData.isEmpty()) {
        dataDirs.append(userData);
    }
    const QString envDataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (!envDataDirs.isEmpty()) {
        dataDirs.append(envDataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts));
    } else {
        dataDirs << QStringLiteral("/usr/local/share") << QStringLiteral("/usr/share");
    }

    const QStringList desktops = currentDesktops();

    QStringList files;
    auto appendIfNew = [&files](const QString& path) {
        if (!path.isEmpty() && !files.contains(path)) {
            files.append(path);
        }
    };

    // Highest precedence first.
    for (const auto& dir : configDirs) {
        appendIfNew(dir + QStringLiteral("/mimeapps.list"));
        for (const auto& desktop : desktops) {
            appendIfNew(dir + QLatin1Char('/') + desktop.toLower() +
                        QStringLiteral("-mimeapps.list"));
            appendIfNew(dir + QLatin1Char('/') + desktop + QStringLiteral("-mimeapps.list"));
        }
    }
    for (const auto& dir : dataDirs) {
        appendIfNew(dir + QStringLiteral("/applications/mimeapps.list"));
        for (const auto& desktop : desktops) {
            appendIfNew(dir + QStringLiteral("/applications/") + desktop.toLower() +
                        QStringLiteral("-mimeapps.list"));
            appendIfNew(dir + QStringLiteral("/applications/") + desktop +
                        QStringLiteral("-mimeapps.list"));
        }
    }
    appendIfNew(QStringLiteral("/usr/share/applications/mimeapps.list"));

    return files;
}

struct MimeAssociations {
    QSet<QString> effective;
    QSet<QString> removed;
};

MimeAssociations readMimeAssociations(const QStringList& mimeTypes) {
    MimeAssociations result;

    for (const auto& filePath : mimeAppsListFiles()) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QSet<QString> addedHere;
        QSet<QString> removedHere;
        QTextStream in(&file);
        QString section;
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            if (line.startsWith(QLatin1Char('['))) {
                section = line;
                continue;
            }
            if (section != QStringLiteral("[Default Applications]") &&
                section != QStringLiteral("[Added Associations]") &&
                section != QStringLiteral("[Removed Associations]")) {
                continue;
            }

            const qsizetype equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            if (!mimeTypes.contains(line.left(equals).trimmed()))
                continue;

            for (const auto& id : splitList(line.mid(equals + 1))) {
                if (section == QStringLiteral("[Removed Associations]")) {
                    removedHere.insert(id);
                } else {
                    addedHere.insert(id);
                }
            }
        }

        // Files are processed from highest to lowest precedence: a removal only
        // affects lower-precedence declarations, while an addition overrides a
        // removal from a lower-precedence file.
        for (const auto& id : removedHere) {
            if (!result.effective.contains(id)) {
                result.removed.insert(id);
            }
        }
        for (const auto& id : addedHere) {
            result.removed.remove(id);
            result.effective.insert(id);
        }
    }

    return result;
}

// Splits an Exec line into arguments, honouring quotes and backslash escapes.
QStringList splitExecArgs(const QString& command) {
    QStringList args;
    QString current;
    bool inSingleQuotes = false;
    bool inDoubleQuotes = false;
    bool escaped = false;

    for (const QChar ch : command) {
        if (escaped) {
            current += ch;
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\') && !inSingleQuotes) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char('"') && !inSingleQuotes) {
            inDoubleQuotes = !inDoubleQuotes;
            continue;
        }
        if (ch == QLatin1Char('\'') && !inDoubleQuotes) {
            inSingleQuotes = !inSingleQuotes;
            continue;
        }
        if (ch.isSpace() && !inSingleQuotes && !inDoubleQuotes) {
            if (!current.isEmpty()) {
                args.append(current);
                current.clear();
            }
            continue;
        }
        current += ch;
    }
    if (!current.isEmpty()) {
        args.append(current);
    }
    return args;
}

QStringList expandExecArgs(const DesktopEntry& entry, const QString& desktopPath,
                           const QStringList& filePaths) {
    const QString firstFile = filePaths.value(0);
    const QString firstUrl = firstFile.isEmpty()
                                 ? QString()
                                 : QUrl::fromLocalFile(firstFile).toString(QUrl::FullyEncoded);

    QStringList result;
    bool hadFileCode = false;

    for (const QString& token : splitExecArgs(entry.exec)) {
        if (token.isEmpty())
            continue;

        if (token == QStringLiteral("%F") || token == QStringLiteral("%U")) {
            hadFileCode = true;
            const bool asUrl = token == QStringLiteral("%U");
            for (const QString& filePath : filePaths) {
                result.append(asUrl ? QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded)
                                    : filePath);
            }
            continue;
        }
        if (token == QStringLiteral("%i")) {
            if (!entry.icon.isEmpty()) {
                result << QStringLiteral("--icon") << entry.icon;
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
            case 'u':
            case 'F':
            case 'U':
                hadFileCode = true;
                expanded +=
                    (code == QLatin1Char('f') || code == QLatin1Char('F')) ? firstFile : firstUrl;
                break;
            case 'c':
                expanded += entry.name;
                break;
            case 'k':
                expanded += desktopPath;
                break;
            case 'i':
                expanded += entry.icon;
                break;
            case '%':
                expanded += QLatin1Char('%');
                break;
            default:
                // Deprecated or unknown field codes are dropped.
                break;
            }
        }
        if (!expanded.isEmpty()) {
            result.append(expanded);
        }
    }

    if (!hadFileCode && !filePaths.isEmpty() && !result.isEmpty()) {
        result += filePaths;
    }
    return result;
}

bool launchWithDesktopEntry(const QString& desktopPath, const QStringList& filePaths) {
    const DesktopEntry entry = readDesktopEntry(desktopPath);
    if (entry.exec.isEmpty())
        return false;

    QStringList args = expandExecArgs(entry, desktopPath, filePaths);
    if (args.isEmpty())
        return false;

    const QString program = args.takeFirst();
    const QString workDir =
        (!entry.path.isEmpty() && QDir(entry.path).exists()) ? entry.path : QString();

    if (entry.terminal) {
        QStringList command{program};
        command += args;
        if (QProcess::startDetached(QStringLiteral("xdg-terminal-exec"), command, workDir)) {
            return true;
        }
        if (QProcess::startDetached(QStringLiteral("x-terminal-emulator"),
                                    QStringList{QStringLiteral("-e")} + command, workDir)) {
            return true;
        }
        // Fall through to a direct launch if no terminal emulator is available.
    }

    return QProcess::startDetached(program, args, workDir);
}

void recordRecent(const QString& filePath) {
    if (!filePath.isEmpty()) {
        RecentFilesManager::add(filePath);
    }
}

void recordRecent(const QStringList& filePaths) {
    for (const QString& filePath : filePaths) {
        recordRecent(filePath);
    }
}

bool isAppImage(const QString& filePath, const QString& mimeType) {
    static const QSet<QString> appImageMimeTypes = {
        QStringLiteral("application/vnd.appimage"), QStringLiteral("application/x-appimage"),
        QStringLiteral("application/x-iso9660-appimage")};

    return appImageMimeTypes.contains(mimeType) ||
           filePath.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive) ||
           filePath.endsWith(QStringLiteral(".appimage"), Qt::CaseInsensitive);
}

QString elfMachineName(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QByteArray header = file.read(20);
    if (header.size() < 20 || !header.startsWith(QByteArrayLiteral("\x7f"
                                                                   "ELF"))) {
        return {};
    }

    const bool littleEndian = static_cast<quint8>(header.at(5)) == 1;
    const auto byteAt = [&header](int index) {
        return static_cast<quint16>(static_cast<quint8>(header.at(index)));
    };
    const quint16 machine =
        littleEndian ? static_cast<quint16>(byteAt(18) | static_cast<quint16>(byteAt(19) << 8))
                     : static_cast<quint16>(static_cast<quint16>(byteAt(18) << 8) | byteAt(19));

    switch (machine) {
    case 3:
        return QStringLiteral("x86 (32-bit)");
    case 62:
        return QStringLiteral("x86_64");
    case 40:
        return QStringLiteral("ARM (32-bit)");
    case 183:
        return QStringLiteral("ARM64 (aarch64)");
    case 243:
        return QStringLiteral("RISC-V");
    case 21:
        return QStringLiteral("PowerPC64");
    case 22:
        return QStringLiteral("s390x");
    case 258:
        return QStringLiteral("LoongArch");
    default:
        return QStringLiteral("an unsupported architecture (ELF machine %1)").arg(machine);
    }
}

bool architecturesMatch(const QString& elfArch, const QString& hostArch) {
    const QString elf = elfArch.toLower();
    const QString host = hostArch.toLower();
    if (elf.isEmpty() || elf.startsWith(QStringLiteral("an unsupported"))) {
        return true;
    }

    if (elf.contains(QStringLiteral("arm64")) || elf.contains(QStringLiteral("aarch64"))) {
        return host.contains(QStringLiteral("arm")) || host.contains(QStringLiteral("aarch64"));
    }
    if (elf.contains(QStringLiteral("x86_64"))) {
        return host.contains(QStringLiteral("x86_64")) || host.contains(QStringLiteral("amd64"));
    }
    if (elf.contains(QStringLiteral("x86"))) {
        return host.contains(QStringLiteral("x86")) || host.contains(QStringLiteral("i386")) ||
               host.contains(QStringLiteral("i686"));
    }
    if (elf.contains(QStringLiteral("risc"))) {
        return host.contains(QStringLiteral("risc"));
    }
    if (elf.contains(QStringLiteral("powerpc")) || elf.contains(QStringLiteral("ppc"))) {
        return host.contains(QStringLiteral("ppc")) || host.contains(QStringLiteral("powerpc"));
    }
    if (elf.contains(QStringLiteral("s390"))) {
        return host.contains(QStringLiteral("s390"));
    }
    if (elf.contains(QStringLiteral("loong"))) {
        return host.contains(QStringLiteral("loong"));
    }
    return true;
}

bool launchAppImage(const QFileInfo& info) {
    const QString path = info.absoluteFilePath();
    QFile::Permissions permissions = info.permissions();
    const bool hasExecBits =
        permissions & (QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    if (!QFileInfo(path).isExecutable()) {
        if (hasExecBits) {
            QMessageBox::warning(QApplication::activeWindow(),
                                 QStringLiteral("Cannot Run AppImage"),
                                 QStringLiteral("\"%1\" is marked executable but cannot be run. "
                                                "It may be on a filesystem mounted with noexec.")
                                     .arg(info.fileName()));
            return false;
        }

        const auto answer = QMessageBox::question(
            QApplication::activeWindow(), QStringLiteral("Run AppImage"),
            QStringLiteral("\"%1\" is not executable.\n\nMake it executable and run it?")
                .arg(info.fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) {
            return false;
        }

        permissions |= QFileDevice::ExeOwner;
        if (permissions & QFileDevice::ReadGroup) {
            permissions |= QFileDevice::ExeGroup;
        }
        if (permissions & QFileDevice::ReadOther) {
            permissions |= QFileDevice::ExeOther;
        }

        if (!QFile::setPermissions(path, permissions) || !QFileInfo(path).isExecutable()) {
            QMessageBox::warning(
                QApplication::activeWindow(), QStringLiteral("Cannot Run AppImage"),
                QStringLiteral("Could not make \"%1\" executable.").arg(info.fileName()));
            return false;
        }
    }

    qint64 pid = 0;
    if (QProcess::startDetached(path, QStringList(), info.absolutePath(), &pid)) {
        recordRecent(path);
        return true;
    }

    QString processError;
    {
        QProcess probe;
        probe.setProgram(path);
        probe.setWorkingDirectory(info.absolutePath());
        probe.start();
        if (probe.waitForStarted(1000)) {
            probe.kill();
            probe.waitForFinished(1000);
            processError = QStringLiteral("the process could not be detached");
        } else {
            processError = probe.errorString();
            if (probe.state() != QProcess::NotRunning) {
                probe.kill();
                probe.waitForFinished(1000);
            }
        }
    }

    const QString elfArch = elfMachineName(path);
    const QString hostArch = QSysInfo::currentCpuArchitecture();
    QString message;
    if (!elfArch.isEmpty() && !architecturesMatch(elfArch, hostArch)) {
        message = QStringLiteral("\"%1\" is built for %2, but this system is %3.\n\n"
                                 "Download the %3 build of this AppImage instead.")
                      .arg(info.fileName(), elfArch, hostArch);
    } else {
        message = QStringLiteral("Could not launch \"%1\".").arg(info.fileName());
        if (!processError.isEmpty()) {
            message += QStringLiteral("\n\n%1").arg(processError);
        }
        if (!elfArch.isEmpty()) {
            message += QStringLiteral("\n\nDetected architecture: %1 (this system: %2).")
                           .arg(elfArch, hostArch);
        }
    }

    QMessageBox::warning(QApplication::activeWindow(), QStringLiteral("Cannot Run AppImage"),
                         message);
    return false;
}

} // namespace

static QStringList candidateMimeTypes(const QString& mimeType) {
    // A handler registered for a parent type (e.g. text/plain for text/x-python)
    // is also a valid handler for the child type.
    QMimeDatabase database;
    QStringList result;

    std::function<void(const QMimeType&)> visit = [&](const QMimeType& type) {
        if (!type.isValid())
            return;

        const QString name = type.name();
        // application/octet-stream is the universal root; matching it would
        // list almost every application.
        if (name.isEmpty() || name == QStringLiteral("application/octet-stream"))
            return;
        if (!result.contains(name)) {
            result.append(name);
        }
        for (const QString& alias : type.aliases()) {
            if (!alias.isEmpty() && !result.contains(alias)) {
                result.append(alias);
            }
        }
        for (const QString& parent : type.parentMimeTypes()) {
            visit(database.mimeTypeForName(parent));
        }
    };

    visit(database.mimeTypeForName(mimeType));
    return result;
}

QStringList DesktopAppsManager::desktopSearchPaths() {
    return applicationDirs();
}

QString DesktopAppsManager::defaultAppIdForMime(const QString& mimeType) {
    if (mimeType.isEmpty())
        return {};

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto cached = s_defaultAppCache.constFind(mimeType);
    if (cached != s_defaultAppCache.constEnd() && now - cached->timestamp < AppsCacheTtlMs) {
        return cached->id;
    }

    auto cacheResult = [&](const QString& id) {
        s_defaultAppCache.insert(mimeType, {now, id});
        return id;
    };

    // 1. Query xdg-mime query default <mimeType>
    QProcess proc;
    proc.start(QStringLiteral("xdg-mime"),
               {QStringLiteral("query"), QStringLiteral("default"), mimeType});
    if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            return cacheResult(out);
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
            return cacheResult(match.captured(1).trimmed());
        }
    }

    // 3. Fallback: parse mimeapps.list files directly (QSettings treats ';' as
    //    a comment and would truncate association lists).
    for (const auto& filePath : mimeAppsListFiles()) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QTextStream in(&file);
        bool inDefaults = false;
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                inDefaults = (line == QStringLiteral("[Default Applications]"));
                continue;
            }
            if (!inDefaults)
                continue;

            const qsizetype equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0)
                continue;
            if (line.left(equals).trimmed() != mimeType)
                continue;

            const QStringList values = splitList(line.mid(equals + 1));
            if (!values.isEmpty()) {
                return cacheResult(values.first());
            }
        }
    }

    return cacheResult({});
}

QVector<AppEntry> DesktopAppsManager::appsForMime(const QString& mimeType) {
    QVector<AppEntry> list;
    if (mimeType.isEmpty())
        return list;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto cached = s_appsForMimeCache.constFind(mimeType);
    if (cached != s_appsForMimeCache.constEnd() && now - cached->timestamp < AppsCacheTtlMs) {
        return cached->apps;
    }

    // Match the type itself, its aliases and its parent types.
    const QStringList candidates = candidateMimeTypes(mimeType);
    const QString defaultId = defaultAppIdForMime(mimeType);
    const MimeAssociations associations = readMimeAssociations(candidates);
    const MimeCache cache = readMimeInfoCache();

    QSet<QString> explicitIds = associations.effective;
    if (!defaultId.isEmpty()) {
        explicitIds.insert(defaultId);
    }

    QSet<QString> ids;
    for (const auto& candidate : candidates) {
        for (const auto& id : cache.appsByMime.value(candidate)) {
            ids.insert(id);
        }
    }
    ids.unite(associations.effective);
    if (!defaultId.isEmpty()) {
        ids.insert(defaultId);
    }

    for (const auto& removed : associations.removed) {
        if (removed != defaultId) {
            ids.remove(removed);
        }
    }

    // No cache (or nothing registered for this type): fall back to scanning
    // desktop files for declarations.
    if (ids.isEmpty()) {
        for (const auto& dirPath : applicationDirs()) {
            const QDir dir(dirPath);
            for (const auto& fi : dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
                const DesktopEntry entry = readDesktopEntry(fi.absoluteFilePath());
                const QStringList mimeList = splitList(entry.mimeType);
                for (const auto& declared : mimeList) {
                    if (candidates.contains(declared)) {
                        ids.insert(fi.fileName());
                        break;
                    }
                }
            }
        }
    }

    QSet<QString> seenIds;
    for (const auto& desktopId : ids) {
        if (seenIds.contains(desktopId))
            continue;

        const QString desktopPath = desktopFileForId(desktopId);
        if (desktopPath.isEmpty())
            continue;

        const DesktopEntry desktopEntry = readDesktopEntry(desktopPath);
        if (!isListable(desktopEntry, explicitIds.contains(desktopId)))
            continue;

        seenIds.insert(desktopId);

        AppEntry entry;
        entry.id = desktopId;
        entry.name = desktopEntry.name;
        if (entry.name.isEmpty()) {
            entry.name = QFileInfo(desktopPath).completeBaseName();
        }
        entry.exec = desktopEntry.exec;
        entry.iconName = desktopEntry.icon;
        entry.icon = QIcon::fromTheme(entry.iconName,
                                      QIcon::fromTheme(QStringLiteral("application-x-executable")));
        entry.isDefault = (desktopId == defaultId);

        list.append(entry);
    }

    // Put default app first, then sort alphabetically by name
    std::sort(list.begin(), list.end(), [](const AppEntry& a, const AppEntry& b) {
        if (a.isDefault != b.isDefault)
            return a.isDefault;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    return list;
}

QVector<AppEntry> DesktopAppsManager::allDesktopApps() {
    QVector<AppEntry> list;
    QSet<QString> seenIds;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (s_allAppsTimestamp > 0 && now - s_allAppsTimestamp < AppsCacheTtlMs) {
        return s_allAppsCache;
    }

    for (const auto& dirPath : applicationDirs()) {
        const QDir dir(dirPath);
        for (const auto& fi : dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            const QString desktopId = fi.fileName();
            if (seenIds.contains(desktopId))
                continue;

            const DesktopEntry desktopEntry = readDesktopEntry(fi.absoluteFilePath());
            if (!isListable(desktopEntry, false))
                continue;

            seenIds.insert(desktopId);

            AppEntry entry;
            entry.id = desktopId;
            entry.name = desktopEntry.name;
            if (entry.name.isEmpty())
                entry.name = fi.completeBaseName();
            entry.exec = desktopEntry.exec;
            entry.iconName = desktopEntry.icon;
            entry.icon = QIcon::fromTheme(
                entry.iconName, QIcon::fromTheme(QStringLiteral("application-x-executable")));

            list.append(entry);
        }
    }

    std::sort(list.begin(), list.end(), [](const AppEntry& a, const AppEntry& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    s_allAppsCache = list;
    s_allAppsTimestamp = now;
    return list;
}

bool DesktopAppsManager::setDefaultAppForMime(const QString& mimeType,
                                              const QString& desktopFileId) {
    if (mimeType.isEmpty() || desktopFileId.isEmpty())
        return false;

    // 1. Run xdg-mime default <desktopFileId> <mimeType>
    bool succeeded = false;
    QProcess xdgProc;
    xdgProc.start(QStringLiteral("xdg-mime"), {QStringLiteral("default"), desktopFileId, mimeType});
    if (xdgProc.waitForFinished(2000) && xdgProc.exitCode() == 0) {
        succeeded = true;
    }

    // 2. Update mimeapps.list ourselves, atomically (XDG_CONFIG_HOME aware).
    //    This also covers systems where xdg-mime is unavailable and avoids a
    //    second blocking gio process.
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!configDir.isEmpty()) {
        QDir().mkpath(configDir);
        const QString mimePath = configDir + QStringLiteral("/mimeapps.list");

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

        auto updateSection = [&](const QString& sectionHeader, const QString& key,
                                 const QString& value, bool appendSemis) {
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
                    lines[keyIdx] =
                        key + QLatin1Char('=') + parts.join(QLatin1Char(';')) + QLatin1Char(';');
                } else {
                    lines[keyIdx] = newLine;
                }
            } else {
                lines.insert(sectionIdx + 1, newLine);
            }
        };

        updateSection(QStringLiteral("[Default Applications]"), mimeType, desktopFileId, false);
        updateSection(QStringLiteral("[Added Associations]"), mimeType, desktopFileId, true);

        // QSaveFile writes atomically: a crash mid-write cannot corrupt the
        // user's entire association database.
        QSaveFile outFile(mimePath);
        if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&outFile);
            for (const auto& line : lines) {
                out << line << QLatin1Char('\n');
            }
            if (outFile.commit()) {
                succeeded = true;
            }
        }
    }

    // 3. Update KDE Plasma sycoca database cache
    QProcess::startDetached(QStringLiteral("kbuildsycoca6"), {QStringLiteral("--noincremental")});

    clearCaches();
    return succeeded;
}

void DesktopAppsManager::clearCaches() {
    s_appsForMimeCache.clear();
    s_allAppsCache.clear();
    s_allAppsTimestamp = 0;
    s_defaultAppCache.clear();
}

bool DesktopAppsManager::launchApp(const QString& desktopFileId, const QString& filePath) {
    return launchApp(desktopFileId, filePath.isEmpty() ? QStringList() : QStringList{filePath});
}

bool DesktopAppsManager::launchApp(const QString& desktopFileId, const QStringList& filePaths) {
    if (desktopFileId.isEmpty())
        return false;

    const QString fullDesktopPath = desktopFileForId(desktopFileId);
    auto success = [&filePaths]() {
        recordRecent(filePaths);
        return true;
    };

    // 1. Try gio launch <fullDesktopPath> [files...]
    if (!fullDesktopPath.isEmpty()) {
        QStringList gioArgs = {QStringLiteral("launch"), fullDesktopPath};
        gioArgs += filePaths;
        if (QProcess::startDetached(QStringLiteral("gio"), gioArgs)) {
            return success();
        }
    }

    // 2. Try gtk-launch <desktopFileId> [files...]
    QStringList gtkArgs = {desktopFileId};
    gtkArgs += filePaths;
    if (QProcess::startDetached(QStringLiteral("gtk-launch"), gtkArgs)) {
        return success();
    }

    // 3. Fallback: expand the Exec line ourselves, respecting quotes, field
    //    codes and the desktop file's working directory.
    if (!fullDesktopPath.isEmpty() && launchWithDesktopEntry(fullDesktopPath, filePaths)) {
        return success();
    }

    return false;
}

bool DesktopAppsManager::launchCustomCommand(const QString& command, const QString& filePath) {
    return launchCustomCommand(command, filePath.isEmpty() ? QStringList() : QStringList{filePath});
}

bool DesktopAppsManager::launchCustomCommand(const QString& command, const QStringList& filePaths) {
    if (command.trimmed().isEmpty()) {
        return false;
    }

    // Reuse the same quoting/field-code parser as desktop files, but never
    // route through a shell: file names supplied by the filesystem must not be
    // able to inject shell commands.
    DesktopEntry entry;
    entry.exec = command;
    QStringList args = expandExecArgs(entry, QString(), filePaths);
    if (args.isEmpty()) {
        return false;
    }

    const QString program = args.takeFirst();
    if (!QProcess::startDetached(program, args)) {
        return false;
    }
    recordRecent(filePaths);
    return true;
}

bool DesktopAppsManager::isAppImageFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        return false;
    }
    QMimeDatabase database;
    return isAppImage(filePath,
                      database.mimeTypeForFile(filePath, QMimeDatabase::MatchDefault).name());
}

bool DesktopAppsManager::launchFile(const QString& filePath) {
    if (filePath.isEmpty() || !QFile::exists(filePath))
        return false;

    QFileInfo fi(filePath);
    if (fi.isDir()) {
        return false;
    }

    QMimeDatabase mimeDb;
    QString mimeType = mimeDb.mimeTypeForFile(filePath, QMimeDatabase::MatchDefault).name();

    // AppImages are applications, not documents. Run them directly instead of
    // letting a package manager (for example Shelly) claim the MIME type.
    if (isAppImage(filePath, mimeType)) {
        return launchAppImage(fi);
    }

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

    // Nothing is registered at all: say so instead of silently doing nothing.
    if (defaultId.isEmpty() && apps.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("Cannot Open File"),
                             QStringLiteral("No application is associated with this file type.\n\n"
                                            "Use Get Info → Open with to choose an application."));
        return false;
    }

    // 3. Fallback: gio open
    if (QProcess::startDetached(QStringLiteral("gio"), {QStringLiteral("open"), filePath})) {
        recordRecent(filePath);
        return true;
    }

    // 4. Fallback: xdg-open
    if (QProcess::startDetached(QStringLiteral("xdg-open"), {filePath})) {
        recordRecent(filePath);
        return true;
    }

    // 5. Final fallback
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    if (opened) {
        recordRecent(filePath);
    } else {
        QMessageBox::warning(nullptr, QStringLiteral("Cannot Open File"),
                             QStringLiteral("Could not launch an application for \"%1\".")
                                 .arg(QFileInfo(filePath).fileName()));
    }
    return opened;
}

bool DesktopAppsManager::openTerminal(const QString& dirPath, QString* error) {
    QString workDir = dirPath;
    if (workDir.isEmpty() || !QDir(workDir).exists()) {
        workDir = QDir::homePath();
    }

    // 1. Try $TERMINAL environment variable if set
    QString envTerm = qEnvironmentVariable("TERMINAL");
    if (!envTerm.isEmpty()) {
        if (QProcess::startDetached(envTerm, QStringList(), workDir)) {
            return true;
        }
    }

    // 2. Try modern standard xdg-terminal-exec
    if (QProcess::startDetached(QStringLiteral("xdg-terminal-exec"), QStringList(), workDir)) {
        return true;
    }

    // 3. Try KDE default konsole with --workdir
    if (QProcess::startDetached(QStringLiteral("konsole"), {QStringLiteral("--workdir"), workDir},
                                workDir)) {
        return true;
    }

    // 4. Try other desktop terminals
    struct TermOption {
        QString binary;
        QStringList args;
    };
    const QList<TermOption> candidates = {
        {QStringLiteral("ptyxis"), {QStringLiteral("--working-directory"), workDir}},
        {QStringLiteral("gnome-terminal"), {QStringLiteral("--working-directory=") + workDir}},
        {QStringLiteral("xfce4-terminal"), {QStringLiteral("--working-directory=") + workDir}},
        {QStringLiteral("kitty"), {QStringLiteral("--directory"), workDir}},
        {QStringLiteral("alacritty"), {QStringLiteral("--working-directory"), workDir}},
        {QStringLiteral("foot"), {QStringLiteral("-D"), workDir}},
        {QStringLiteral("x-terminal-emulator"), {}},
        {QStringLiteral("xterm"), {}}};

    for (const auto& cand : candidates) {
        if (QProcess::startDetached(cand.binary, cand.args, workDir)) {
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("Could not find a terminal emulator installed.");
    }
    return false;
}
