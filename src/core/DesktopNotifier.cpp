#include "DesktopNotifier.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QVariantList>
#include <QVariantMap>

void DesktopNotifier::notify(const QString& summary, const QString& body)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->isServiceRegistered(
            QStringLiteral("org.freedesktop.Notifications"))) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));

    QVariantList arguments;
    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("finder-linux"));
    arguments << QCoreApplication::applicationName()   // app_name
              << 0u                                    // replaces_id
              << QStringLiteral("finder-linux")        // app_icon
              << summary
              << body
              << QStringList()                         // actions
              << hints                                  // hints
              << 5000;                                 // timeout (ms)

    message.setArguments(arguments);
    bus.send(message);
}
