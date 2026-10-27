#include "AppSettings.h"
#include "VolumeManager.h"
#include "XdgTrash.h"
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
    m_favoriteOrder = s.value(QStringLiteral("favoriteOrder")).toStringList();
    m_hiddenFavorites = s.value(QStringLiteral("hiddenFavorites")).toStringList();
    m_hiddenLocations = s.value(QStringLiteral("hiddenLocations")).toStringList();
    m_locationOrder = s.value(QStringLiteral("locationOrder")).toStringList();
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
    s.setValue(QStringLiteral("favoriteOrder"), m_favoriteOrder);
    s.setValue(QStringLiteral("hiddenFavorites"), m_hiddenFavorites);
    s.setValue(QStringLiteral("hiddenLocations"), m_hiddenLocations);
    s.setValue(QStringLiteral("locationOrder"), m_locationOrder);
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
    }
    if (m_favoriteOrder.isEmpty()) {
        m_favoriteOrder = favoriteOrder();
    } else if (!m_favoriteOrder.contains(path)) {
        m_favoriteOrder.append(path);
    }
    save();
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
    if (m_favoriteOrder.removeAll(path) > 0) {
        changed = true;
    }
    if (changed) {
        save();
    }
}

QStringList AppSettings::defaultFavorites()
{
    QStringList defaults;
    defaults << QDir::homePath();
    auto addStd = [&](QStandardPaths::StandardLocation loc) {
        QString p = QStandardPaths::writableLocation(loc);
        if (!p.isEmpty() && !defaults.contains(p)) defaults << p;
    };
    addStd(QStandardPaths::DesktopLocation);
    addStd(QStandardPaths::DocumentsLocation);
    addStd(QStandardPaths::DownloadLocation);
    addStd(QStandardPaths::PicturesLocation);
    addStd(QStandardPaths::MusicLocation);
    addStd(QStandardPaths::MoviesLocation);
    return defaults;
}

QStringList AppSettings::favoriteOrder() const
{
    const QStringList defaults = defaultFavorites();
    QStringList allKnown = defaults;
    for (const auto& fav : m_customFavorites) {
        if (!allKnown.contains(fav)) allKnown.append(fav);
    }

    if (m_favoriteOrder.isEmpty()) {
        return allKnown;
    }

    QStringList result;
    // Follow the saved order for any known item
    for (const auto& path : m_favoriteOrder) {
        if (allKnown.contains(path) && !result.contains(path)) {
            result.append(path);
        }
    }
    // Append any known items not yet in saved order
    for (const auto& path : allKnown) {
        if (!result.contains(path)) {
            result.append(path);
        }
    }
    return result;
}

void AppSettings::setFavoriteOrder(const QStringList& order)
{
    m_favoriteOrder = order;
    save();
}

void AppSettings::reorderFavorite(const QString& path, int newIndex)
{
    QStringList order = favoriteOrder();
    if (!order.contains(path)) {
        order.append(path);
    }
    order.removeAll(path);
    int clampedIndex = qBound(0, newIndex, static_cast<int>(order.size()));
    order.insert(clampedIndex, path);
    setFavoriteOrder(order);
}

void AppSettings::moveFavoriteUp(const QString& path)
{
    QStringList order = favoriteOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx > 0) {
        order.swapItemsAt(idx, idx - 1);
        setFavoriteOrder(order);
    }
}

void AppSettings::moveFavoriteDown(const QString& path)
{
    QStringList order = favoriteOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx >= 0 && idx < static_cast<int>(order.size()) - 1) {
        order.swapItemsAt(idx, idx + 1);
        setFavoriteOrder(order);
    }
}

void AppSettings::moveFavoriteToTop(const QString& path)
{
    QStringList order = favoriteOrder();
    order.removeAll(path);
    order.prepend(path);
    setFavoriteOrder(order);
}

void AppSettings::moveFavoriteToBottom(const QString& path)
{
    QStringList order = favoriteOrder();
    order.removeAll(path);
    order.append(path);
    setFavoriteOrder(order);
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
    m_favoriteOrder.clear();
    m_locationOrder.clear();
    m_hiddenFavorites.clear();
    m_hiddenLocations.clear();
    save();
}

QStringList AppSettings::locationOrder() const
{
    QStringList allKnown;
    const auto volumes = VolumeManager::mountedVolumes();
    for (const auto& v : volumes) {
        if (!allKnown.contains(v.path)) {
            allKnown.append(v.path);
        }
    }
    QString trashPath = XdgTrash::trashFilesDirectory();
    if (!allKnown.contains(trashPath)) {
        allKnown.append(trashPath);
    }

    if (m_locationOrder.isEmpty()) {
        return allKnown;
    }

    QStringList result;
    for (const auto& path : m_locationOrder) {
        if (allKnown.contains(path) && !result.contains(path)) {
            result.append(path);
        }
    }
    for (const auto& path : allKnown) {
        if (!result.contains(path)) {
            result.append(path);
        }
    }
    return result;
}

void AppSettings::setLocationOrder(const QStringList& order)
{
    m_locationOrder = order;
    save();
}

void AppSettings::reorderLocation(const QString& path, int newIndex)
{
    QStringList order = locationOrder();
    if (!order.contains(path)) {
        order.append(path);
    }
    order.removeAll(path);
    int clampedIndex = qBound(0, newIndex, static_cast<int>(order.size()));
    order.insert(clampedIndex, path);
    setLocationOrder(order);
}

void AppSettings::moveLocationUp(const QString& path)
{
    QStringList order = locationOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx > 0) {
        order.swapItemsAt(idx, idx - 1);
        setLocationOrder(order);
    }
}

void AppSettings::moveLocationDown(const QString& path)
{
    QStringList order = locationOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx >= 0 && idx < static_cast<int>(order.size()) - 1) {
        order.swapItemsAt(idx, idx + 1);
        setLocationOrder(order);
    }
}

void AppSettings::moveLocationToTop(const QString& path)
{
    QStringList order = locationOrder();
    order.removeAll(path);
    order.prepend(path);
    setLocationOrder(order);
}

void AppSettings::moveLocationToBottom(const QString& path)
{
    QStringList order = locationOrder();
    order.removeAll(path);
    order.append(path);
    setLocationOrder(order);
}
