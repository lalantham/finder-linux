#include "DbusActivation.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

namespace DbusActivation {

bool installServiceFile(const QString& wellKnownName, const QString& executable, QString* error)
{
    if (wellKnownName.isEmpty() || wellKnownName.contains(QLatin1Char('/'))) {
        if (error) *error = QStringLiteral("Invalid D-Bus service name.");
        return false;
    }
    if (executable.isEmpty()) {
        if (error) *error = QStringLiteral("Empty executable path.");
        return false;
    }

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataDir.isEmpty()) {
        if (error) *error = QStringLiteral("Could not determine the user data directory.");
        return false;
    }

    const QString serviceDir = dataDir + QStringLiteral("/dbus-1/services");
    if (!QDir().mkpath(serviceDir)) {
        if (error) *error = QStringLiteral("Could not create %1").arg(serviceDir);
        return false;
    }

    QString quotedExecutable = executable;
    quotedExecutable.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    quotedExecutable.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    const QString servicePath = serviceDir + QLatin1Char('/') + wellKnownName + QStringLiteral(".service");
    QSaveFile file(servicePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Could not write %1").arg(servicePath);
        return false;
    }

    QTextStream out(&file);
    out << "[D-BUS Service]\n"
        << "Name=" << wellKnownName << "\n"
        << "Exec=\"" << quotedExecutable << "\"\n";
    out.flush();
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Could not write %1").arg(servicePath);
        return false;
    }

    // Make the session bus re-read activation files so the new service is
    // available without requiring a logout.
    QDBusMessage reload = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"),
        QStringLiteral("ReloadConfig"));
    QDBusConnection::sessionBus().send(reload);

    return true;
}

} // namespace DbusActivation
