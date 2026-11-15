#include "AppSettings.h"
#include "ApplicationService.h"
#include "FileManager1Service.h"
#include "FileSystemModel.h"
#include "MainWindow.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSessionManager>
#include <QSet>
#include <QUrl>
#include <memory>
#include <unistd.h>

#ifndef FINDER_VERSION
#define FINDER_VERSION "1.0.0"
#endif

static QString resolveInputPath(const QString& input) {
    if (input.isEmpty())
        return {};
    QUrl url = QUrl::fromUserInput(input, QDir::currentPath(), QUrl::AssumeLocalFile);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return input;
}

static bool isRemoteUri(const QString& input) {
    const QUrl url(input);
    if (!url.isValid() || url.scheme().isEmpty() || url.isLocalFile()) {
        return false;
    }
    static const QSet<QString> schemes = {QStringLiteral("sftp"), QStringLiteral("smb"),
                                          QStringLiteral("ftp"),  QStringLiteral("ftps"),
                                          QStringLiteral("dav"),  QStringLiteral("davs"),
                                          QStringLiteral("ssh"),  QStringLiteral("nfs"),
                                          QStringLiteral("mtp"),  QStringLiteral("afp")};
    return schemes.contains(url.scheme().toLower());
}

namespace {

QString sessionSocketName() {
    return QStringLiteral("finder-linux-%1").arg(::getuid());
}

bool forwardToRunningInstance(const QStringList& paths, const QString& selectTarget) {
    QLocalSocket socket;
    socket.connectToServer(sessionSocketName());
    if (!socket.waitForConnected(300)) {
        return false;
    }

    QByteArray payload;
    {
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << paths << selectTarget;
    }

    QByteArray block;
    {
        QDataStream stream(&block, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << quint32(payload.size());
        block.append(payload);
    }

    if (socket.write(block) < 0) {
        return false;
    }
    socket.flush();
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

void applyOpenRequest(MainWindow& window, const QStringList& paths, const QString& selectTarget) {
    for (const auto& path : paths) {
        if (isRemoteUri(path)) {
            window.openRemoteLocation(path);
            continue;
        }
        QFileInfo info(path);
        if (info.isDir()) {
            window.openInNewTab(info.absoluteFilePath());
        } else if (info.exists() || info.isSymLink()) {
            window.openAndSelect(info.absoluteFilePath());
        }
    }
    if (!selectTarget.isEmpty()) {
        window.openAndSelect(selectTarget);
    }
    window.raise();
    window.activateWindow();
}

void startInstanceServer(MainWindow& window) {
    static QLocalServer server;
    if (server.isListening()) {
        return;
    }

    // A stale socket from a crashed instance would make listen() fail.
    QLocalServer::removeServer(sessionSocketName());
    if (!server.listen(sessionSocketName())) {
        return;
    }

    QObject::connect(&server, &QLocalServer::newConnection, &server, [&window]() {
        while (QLocalSocket* connection = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(connection, &QLocalSocket::readyRead, connection,
                             [connection, buffer, &window]() {
                                 buffer->append(connection->readAll());
                                 if (buffer->size() < static_cast<int>(sizeof(quint32))) {
                                     return;
                                 }

                                 QDataStream sizeStream(*buffer);
                                 sizeStream.setVersion(QDataStream::Qt_6_0);
                                 quint32 blockSize = 0;
                                 sizeStream >> blockSize;
                                 if (buffer->size() < static_cast<int>(sizeof(quint32)) +
                                                          static_cast<int>(blockSize)) {
                                     return;
                                 }

                                 const QByteArray payload =
                                     buffer->mid(sizeof(quint32), static_cast<int>(blockSize));
                                 buffer->clear();

                                 QDataStream stream(payload);
                                 stream.setVersion(QDataStream::Qt_6_0);
                                 QStringList paths;
                                 QString selectTarget;
                                 stream >> paths >> selectTarget;
                                 applyOpenRequest(window, paths, selectTarget);

                                 connection->disconnectFromServer();
                             });
            QObject::connect(connection, &QLocalSocket::disconnected, connection,
                             &QObject::deleteLater);
        }
    });
}

} // namespace

int main(int argc, char* argv[]) {
    // Avoid flooding the journal with FFmpeg stream metadata when Preview
    // opens a video. Developers can still set QT_FFMPEG_DEBUG themselves.
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DEBUG")) {
        qputenv("QT_FFMPEG_DEBUG", QByteArrayLiteral("0"));
    }

    QApplication app(argc, argv);
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     []() { AppSettings::instance().flush(); });
    app.setApplicationName(QStringLiteral("File Manager"));
    app.setApplicationDisplayName(QStringLiteral("File Manager"));
    app.setOrganizationName(QStringLiteral("FinderLinux"));
    app.setOrganizationDomain(QStringLiteral("finder.linux"));
    app.setApplicationVersion(QStringLiteral(FINDER_VERSION));
    app.setDesktopFileName(QStringLiteral("org.finderlinux.Finder"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("finder-linux"),
                                       QIcon(QStringLiteral(":/icons/finder-linux.svg"))));

    // Apply configured theme
    ThemeManager::instance().applyTheme(AppSettings::instance().theme());

    // Directory scans run in the background so large and network folders do
    // not block the UI.
    FileSystemModel::setAsyncLoadingEnabled(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("File manager for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption selectOption(
        QStringList() << QStringLiteral("s") << QStringLiteral("select"),
        QStringLiteral("The file or folder to select in its parent directory"),
        QStringLiteral("target"));
    parser.addOption(selectOption);

    QCommandLineOption newWindowOption(
        QStringList() << QStringLiteral("n") << QStringLiteral("new-window"),
        QStringLiteral("Open a new window even if File Manager is already running."));
    parser.addOption(newWindowOption);

    parser.addPositionalArgument(QStringLiteral("paths"),
                                 QStringLiteral("Files or directories to open"),
                                 QStringLiteral("[paths...]"));

    parser.process(app);

    const bool forceNewWindow = parser.isSet(newWindowOption);

    // When dbus-daemon started us to handle an org.freedesktop.Application or
    // FileManager1 call, do not forward to the local single-instance socket:
    // that would exit before registering the name and the pending call would
    // be lost. A DBus-activated process also passes DBUS_STARTER_BUS_TYPE to
    // child processes, so only treat it as an activation when the D-Bus names
    // are not already owned by a running File Manager. This keeps normal launches
    // from terminals that inherited the variable working as single-instance.
    bool serviceNamesOwned = false;
    auto* busInterface = QDBusConnection::sessionBus().interface();
    if (busInterface) {
        const bool applicationOwned =
            busInterface->isServiceRegistered(QStringLiteral("org.finderlinux.Finder")).value();
        const bool fileManagerOwned =
            busInterface->isServiceRegistered(QStringLiteral("org.freedesktop.FileManager1"))
                .value();
        serviceNamesOwned = applicationOwned && fileManagerOwned;
    }
    const bool dbusActivated =
        qEnvironmentVariable("DBUS_STARTER_BUS_TYPE") == QLatin1String("session") &&
        !serviceNamesOwned;

    QString selectTarget;
    if (parser.isSet(selectOption)) {
        selectTarget = resolveInputPath(parser.value(selectOption));
    }

    QStringList paths;
    if (selectTarget.isEmpty()) {
        for (const QString& raw : parser.positionalArguments()) {
            const QString resolved = resolveInputPath(raw);
            if (resolved.isEmpty())
                continue;
            if (isRemoteUri(resolved)) {
                paths.append(resolved);
                continue;
            }
            QFileInfo info(resolved);
            if (info.exists() || info.isSymLink()) {
                paths.append(info.absoluteFilePath());
            }
        }
    }

    // Reuse the running instance: folders open as tabs instead of spawning
    // duplicate windows (this is what "default file manager" integration needs).
    if (!forceNewWindow && !dbusActivated && forwardToRunningInstance(paths, selectTarget)) {
        return 0;
    }

    MainWindow window;

    for (int i = 0; i < paths.size(); ++i) {
        if (isRemoteUri(paths.at(i))) {
            window.openRemoteLocation(paths.at(i));
            continue;
        }
        QFileInfo info(paths.at(i));
        if (i == 0) {
            if (info.isDir()) {
                window.openPath(info.absoluteFilePath());
            } else {
                window.openAndSelect(info.absoluteFilePath());
            }
        } else {
            window.openInNewTab(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
        }
    }

    if (!selectTarget.isEmpty()) {
        window.openAndSelect(selectTarget);
    }

    if (!forceNewWindow) {
        startInstanceServer(window);

        // Let browsers and other applications open folders in File Manager via the
        // freedesktop.org FileManager1 D-Bus interface ("Show in folder").
        auto* fileManager1 = new FileManager1Service(window, &app);
        fileManager1->registerOnBus();

        // Standard desktop application activation: launchers call Activate()
        // or Open() on org.finderlinux.Finder instead of spawning a second process.
        auto* application = new ApplicationService(window, &app);
        application->registerOnBus();
    }

    // Persist tabs/geometry when the desktop session ends, not just when the
    // window is closed manually.
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &window,
                     [&window](QSessionManager&) { window.saveSessionState(); });
    QObject::connect(&app, &QGuiApplication::saveStateRequest, &window,
                     [&window](QSessionManager&) { window.saveSessionState(); });

    window.show();
    return app.exec();
}
