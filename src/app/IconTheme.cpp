#include "IconTheme.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace IconTheme {
namespace {

// Reads a single key from an ini file without caring whether it exists.
QString iniValue(const QString& path, const QString& key) {
    if (!QFileInfo::exists(path)) {
        return {};
    }
    QSettings settings(path, QSettings::IniFormat);
    return settings.value(key).toString();
}

} // namespace

QStringList hostThemeSearchPaths() {
    const QString home = QDir::homePath();
    QStringList paths = {
        home + QStringLiteral("/.local/share/icons"),
        home + QStringLiteral("/.icons"),
        home + QStringLiteral("/.local/share/flatpak/exports/share/icons"),
        QStringLiteral("/usr/local/share/icons"),
        QStringLiteral("/usr/share/icons"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons"),
        QStringLiteral("/var/lib/snapd/desktop/icons"),
    };
    paths.removeDuplicates();
    return paths;
}

bool themeAvailable(const QString& theme, const QStringList& searchPaths) {
    if (theme.trimmed().isEmpty()) {
        return false;
    }
    for (const QString& root : searchPaths) {
        if (root.startsWith(QLatin1Char(':'))) {
            continue; // Qt's built-in resources cannot hold a host theme
        }
        if (QFileInfo(root + QLatin1Char('/') + theme + QStringLiteral("/index.theme")).isFile()) {
            return true;
        }
    }
    return false;
}

QString configuredTheme(const QString& homePath) {
    const QString home = homePath.isEmpty() ? QDir::homePath() : homePath;
    // Availability is judged against the host icon roots, independent of what Qt
    // currently has configured - the point is to find what the *desktop* uses.
    const QStringList searchPaths = {
        home + QStringLiteral("/.local/share/icons"),
        home + QStringLiteral("/.icons"),
        home + QStringLiteral("/.local/share/flatpak/exports/share/icons"),
        QStringLiteral("/usr/local/share/icons"),
        QStringLiteral("/usr/share/icons"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons"),
        QStringLiteral("/var/lib/snapd/desktop/icons"),
    };

    // KDE / Plasma. Home first, then the system-wide file Plasma also honours.
    for (const QString& path :
         {home + QStringLiteral("/.config/kdeglobals"), QStringLiteral("/etc/xdg/kdeglobals")}) {
        const QString theme = iniValue(path, QStringLiteral("Icons/Theme"));
        if (themeAvailable(theme, searchPaths)) {
            return theme;
        }
    }

    // GTK desktops: GNOME, Xfce, Cinnamon, Budgie, Unity, elementary.
    // GTK 4 first, because newer desktops migrate there and may leave a stale
    // value behind in the GTK 3 file.
    for (const QString& path : {home + QStringLiteral("/.config/gtk-4.0/settings.ini"),
                                home + QStringLiteral("/.config/gtk-3.0/settings.ini"),
                                QStringLiteral("/etc/gtk-4.0/settings.ini"),
                                QStringLiteral("/etc/gtk-3.0/settings.ini")}) {
        const QString theme = iniValue(path, QStringLiteral("Settings/gtk-icon-theme-name"));
        if (themeAvailable(theme, searchPaths)) {
            return theme;
        }
    }

    // LXQt keeps its own session file.
    const QString lxqt = iniValue(home + QStringLiteral("/.config/lxqt/lxqt.conf"),
                                  QStringLiteral("General/icon_theme"));
    if (themeAvailable(lxqt, searchPaths)) {
        return lxqt;
    }

    return {};
}

QString resolveTheme(const QString& configured, const QString& qtGuess,
                     const QStringList& searchPaths) {
    // 1. What the user configured for their desktop. This is the whole point:
    //    Qt's guess must never override an explicit setting.
    if (themeAvailable(configured, searchPaths)) {
        return configured;
    }

    // 2. A usable theme the platform theme already resolved.
    if (themeAvailable(qtGuess, searchPaths)) {
        return qtGuess;
    }

    // 3. Nothing usable: pick something installed so the UI is not blank.
    for (const QString& candidate : {QStringLiteral("Adwaita"), QStringLiteral("breeze"),
                                     QStringLiteral("Papirus"), QStringLiteral("Tela")}) {
        if (themeAvailable(candidate, searchPaths)) {
            return candidate;
        }
    }
    return {};
}

} // namespace IconTheme
