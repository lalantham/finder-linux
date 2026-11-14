#include "FileManager1Service.h"
#include "MainWindow.h"
#include "DbusActivation.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDebug>
#include <QUrl>

namespace {

QStringList toPaths(const QStringList& uris)
{
    QStringList paths;
    paths.reserve(uris.size());

    for (const QString& raw : uris) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const QUrl url(trimmed);
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty()) {
                paths.append(path);
            }
        } else if (url.isValid() && !url.scheme().isEmpty()) {
            // Keep remote locations (sftp://, smb://, ...) intact so the
            // existing remote-location handling can connect to them.
            paths.append(trimmed);
        } else {
            // Tolerate callers that pass a plain filesystem path.
            paths.append(trimmed);
        }
    }

    return paths;
}

} // namespace

FileManager1Service::FileManager1Service(MainWindow& window, QObject* parent)
    : QObject(parent)
    , m_window(window)
{
}

bool FileManager1Service::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "File Manager: session D-Bus is unavailable;"
                   << "org.freedesktop.FileManager1 integration is disabled";
        return false;
    }

    const QString objectPath = QStringLiteral("/org/freedesktop/FileManager1");
    if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAllSlots)) {
        qWarning() << "File Manager: failed to export" << objectPath << "on the session bus";
        return false;
    }

    // Export the object before claiming the name so an activation request
    // cannot arrive before the method handlers exist.
    QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply =
        bus.interface()->registerService(
            QStringLiteral("org.freedesktop.FileManager1"),
            QDBusConnectionInterface::ReplaceExistingService,
            QDBusConnectionInterface::AllowReplacement);

    if (!reply.isValid()) {
        qWarning() << "File Manager: could not register org.freedesktop.FileManager1:"
                   << reply.error().message();
        return false;
    }

    if (reply.value() == QDBusConnectionInterface::ServiceQueued) {
        // Some other file-manager daemon (for example elementary Files) owns
        // the name and does not allow replacement. We stay queued so we take
        // over automatically as soon as that daemon exits. The activation file
        // installed by install.sh/update.sh makes File Manager win on the next login.
        qInfo() << "File Manager: another file manager owns org.freedesktop.FileManager1;"
                << "queued to take over when it exits";
        return false;
    }

    return true;
}

bool FileManager1Service::installActivationFile(QString* error)
{
    return DbusActivation::installServiceFile(
        QStringLiteral("org.freedesktop.FileManager1"),
        QCoreApplication::applicationFilePath(),
        error);
}

void FileManager1Service::ShowItems(const QStringList& uris, const QString& startupId)
{
    const QStringList paths = toPaths(uris);
    if (paths.isEmpty()) {
        return;
    }
    m_window.setActivationToken(startupId);
    m_window.showItems(paths);
}

void FileManager1Service::ShowItemProperties(const QStringList& uris, const QString& startupId)
{
    const QStringList paths = toPaths(uris);
    if (paths.isEmpty()) {
        return;
    }
    m_window.setActivationToken(startupId);
    m_window.showItemProperties(paths);
}
