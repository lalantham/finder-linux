#pragma once

#include <QString>

namespace DesktopNotifier {

// Sends a desktop notification via org.freedesktop.Notifications.
// Does nothing when no notification daemon is available.
void notify(const QString& summary, const QString& body);

} // namespace DesktopNotifier
