#pragma once

#include <QString>
#include <QStringList>

// Resolving the desktop's icon theme.
//
// Qt only learns the icon theme from the platform theme plugin. Inside an
// AppImage (and in any build where that plugin is missing) Qt cannot load it and
// falls back to a *guess*: its generic unix theme hard-codes "breeze" on a KDE
// host. Trusting that guess made the AppImage render Breeze icons while the
// same binary installed natively rendered the user's real theme.
//
// The rule is therefore: an explicitly configured theme always wins over Qt's
// guess. This lives in its own translation unit so the precedence can be tested
// without a display or a real desktop session.
namespace IconTheme {

// Directories that can contain host icon themes. Qt omits the per-user ones when
// the platform theme fails, so they are added explicitly.
[[nodiscard]] QStringList hostThemeSearchPaths();

// Reads the theme the user configured for their desktop, in order of authority:
// KDE (kdeglobals), GTK 4, GTK 3, LXQt. Returns an empty string when nothing is
// configured or the configured theme is not installed.
[[nodiscard]] QString configuredTheme(const QString& homePath = QString());

// True when `theme` exists in one of `searchPaths` (needs an index.theme).
[[nodiscard]] bool themeAvailable(const QString& theme, const QStringList& searchPaths);

// The theme to apply, given Qt's current guess and the available search paths.
// `configured` wins when it is usable; otherwise a usable Qt guess is kept;
// otherwise a sensible installed default is chosen. Empty means "leave Qt alone".
[[nodiscard]] QString resolveTheme(const QString& configured, const QString& qtGuess,
                                   const QStringList& searchPaths);

} // namespace IconTheme
