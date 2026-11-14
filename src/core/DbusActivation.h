#pragma once

#include <QString>

namespace DbusActivation {

// Writes a per-user D-Bus activation file for `wellKnownName` that launches
// `executable`, then asks the session bus to reload its configuration.
// The file is written atomically and the executable path is quoted.
bool installServiceFile(const QString& wellKnownName, const QString& executable,
                        QString* error = nullptr);

} // namespace DbusActivation
