#include "AppSettings.h"
#include <QDir>
#include <QStandardPaths>

AppSettings& AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
{
    m_defaultPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (m_defaultPath.isEmpty()) {
        m_defaultPath = QDir::homePath();
    }
    load();
}

void AppSettings::load()
{
    QSettings s(QStringLiteral("FinderLinux"), QStringLiteral("FinderLinux"));
    m_viewMode = static_cast<ViewMode>(s.value(QStringLiteral("viewMode"), static_cast<int>(ViewMode::Icon)).toInt());
    m_showHiddenFiles = s.value(QStringLiteral("showHiddenFiles"), false).toBool();
    m_directoriesFirst = s.value(QStringLiteral("directoriesFirst"), true).toBool();
    m_iconSize = s.value(QStringLiteral("iconSize"), 64).toInt();
    m_theme = static_cast<AppTheme>(s.value(QStringLiteral("theme"), static_cast<int>(AppTheme::Light)).toInt());
    m_customFavorites = s.value(QStringLiteral("customFavorites")).toStringList();
    m_windowGeometry = s.value(QStringLiteral("windowGeometry")).toByteArray();
    m_windowState = s.value(QStringLiteral("windowState")).toByteArray();
}

void AppSettings::save()
{
    QSettings s(QStringLiteral("FinderLinux"), QStringLiteral("FinderLinux"));
    s.setValue(QStringLiteral("viewMode"), static_cast<int>(m_viewMode));
    s.setValue(QStringLiteral("showHiddenFiles"), m_showHiddenFiles);
    s.setValue(QStringLiteral("directoriesFirst"), m_directoriesFirst);
    s.setValue(QStringLiteral("iconSize"), m_iconSize);
    s.setValue(QStringLiteral("theme"), static_cast<int>(m_theme));
    s.setValue(QStringLiteral("customFavorites"), m_customFavorites);
    s.setValue(QStringLiteral("windowGeometry"), m_windowGeometry);
    s.setValue(QStringLiteral("windowState"), m_windowState);
}

void AppSettings::addFavorite(const QString& path)
{
    if (!m_customFavorites.contains(path)) {
        m_customFavorites.append(path);
        save();
    }
}

void AppSettings::removeFavorite(const QString& path)
{
    if (m_customFavorites.removeAll(path) > 0) {
        save();
    }
}
