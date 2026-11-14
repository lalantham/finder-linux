#include "ApplicationService.h"
#include "MainWindow.h"
#include "DbusActivation.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDebug>

namespace {
constexpr char ApplicationBusName[] = "org.finderlinux.Finder";

QString activationTokenFrom(const QVariantMap& platformData)
{
    QString token = platformData.value(QStringLiteral("activation-token")).toString();
    if (token.isEmpty()) {
        token = platformData.value(QStringLiteral("desktop-startup-id")).toString();
    }
    return token;
}
}

ApplicationService::ApplicationService(MainWindow& window, QObject* parent)
    : QObject(parent)
    , m_window(window)
{
}

bool ApplicationService::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "Finder: session D-Bus is unavailable;"
                   << "org.freedesktop.Application integration is disabled";
        return false;
    }

    const QString objectPath = QStringLiteral("/org/finderlinux/Finder");
    if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAllSlots)) {
        qWarning() << "Finder: failed to export" << objectPath << "on the session bus";
        return false;
    }

    // Export the object before claiming the name so an activation request
    // cannot arrive before the method handlers exist.
    QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply =
        bus.interface()->registerService(
            QString::fromLatin1(ApplicationBusName),
            QDBusConnectionInterface::ReplaceExistingService,
            QDBusConnectionInterface::AllowReplacement);

    if (!reply.isValid()) {
        qWarning() << "Finder: could not register" << ApplicationBusName
                   << "on the session bus:" << reply.error().message();
        return false;
    }

    if (reply.value() == QDBusConnectionInterface::ServiceQueued) {
        qInfo() << "Finder: another instance owns" << ApplicationBusName
                << "; queued to take over when it exits";
        return false;
    }

    return true;
}

bool ApplicationService::installActivationFile(QString* error)
{
    return DbusActivation::installServiceFile(
        QString::fromLatin1(ApplicationBusName),
        QCoreApplication::applicationFilePath(),
        error);
}

void ApplicationService::Activate(const QVariantMap& platformData)
{
    m_window.setActivationToken(activationTokenFrom(platformData));
    m_window.activateFromDesktop();
}

void ApplicationService::Open(const QStringList& uris, const QVariantMap& platformData)
{
    m_window.setActivationToken(activationTokenFrom(platformData));
    if (uris.isEmpty()) {
        m_window.activateFromDesktop();
        return;
    }
    m_window.showItems(uris);
}

void ApplicationService::ActivateAction(const QString& actionName,
                                        const QVariantList& parameter,
                                        const QVariantMap& platformData)
{
    m_window.setActivationToken(activationTokenFrom(platformData));
    m_window.activateDesktopAction(actionName, parameter);
}
