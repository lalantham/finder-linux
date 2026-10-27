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
    m_theme = static_cast<AppTheme>(s.value(QStringLiteral("theme"), static_cast<int>(AppTheme::System)).toInt());
    m_customFavorites = s.value(QStringLiteral("customFavorites")).toStringList();
    m_hiddenFavorites = s.value(QStringLiteral("hiddenFavorites")).toStringList();
    m_hiddenLocations = s.value(QStringLiteral("hiddenLocations")).toStringList();
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
    s.setValue(QStringLiteral("hiddenFavorites"), m_hiddenFavorites);
    s.setValue(QStringLiteral("hiddenLocations"), m_hiddenLocations);
    s.setValue(QStringLiteral("windowGeometry"), m_windowGeometry);
    s.setValue(QStringLiteral("windowState"), m_windowState);
}

void AppSettings::addFavorite(const QString& path)
{
    if (m_hiddenFavorites.contains(path)) {
        m_hiddenFavorites.removeAll(path);
    }
    if (!m_customFavorites.contains(path)) {
        m_customFavorites.append(path);
        save();
    } else {
        save();
    }
}

void AppSettings::removeFavorite(const QString& path)
{
    bool changed = false;
    if (m_customFavorites.removeAll(path) > 0) {
        changed = true;
    }
    if (m_hiddenFavorites.removeAll(path) > 0) {
        changed = true;
    }
    if (changed) {
        save();
    }
}

void AppSettings::setHiddenFavorites(const QStringList& list)
{
    m_hiddenFavorites = list;
    save();
}

void AppSettings::hideFavorite(const QString& path)
{
    if (!m_hiddenFavorites.contains(path)) {
        m_hiddenFavorites.append(path);
        save();
    }
}

void AppSettings::unhideFavorite(const QString& path)
{
    if (m_hiddenFavorites.removeAll(path) > 0) {
        save();
    }
}

void AppSettings::unhideAllFavorites()
{
    if (!m_hiddenFavorites.isEmpty()) {
        m_hiddenFavorites.clear();
        save();
    }
}

void AppSettings::setHiddenLocations(const QStringList& list)
{
    m_hiddenLocations = list;
    save();
}

void AppSettings::hideLocation(const QString& pathOrName)
{
    if (!m_hiddenLocations.contains(pathOrName)) {
        m_hiddenLocations.append(pathOrName);
        save();
    }
}

void AppSettings::unhideLocation(const QString& pathOrName)
{
    if (m_hiddenLocations.removeAll(pathOrName) > 0) {
        save();
    }
}

void AppSettings::unhideAllLocations()
{
    if (!m_hiddenLocations.isEmpty()) {
        m_hiddenLocations.clear();
        save();
    }
}

void AppSettings::resetSidebarDefaults()
{
    m_hiddenFavorites.clear();
    m_hiddenLocations.clear();
    save();
}
