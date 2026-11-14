#include "AppSettings.h"
#include "VolumeManager.h"
#include "XdgTrash.h"
#include <QDir>
#include <QStandardPaths>

static QString s_customSettingsPath;

AppSettings& AppSettings::instance() {
    static AppSettings settings;
    return settings;
}

void AppSettings::setSettingsFilePath(const QString& path) {
    s_customSettingsPath = path;
    instance().load();
}

namespace {

QVariantMap filterToMap(const SearchFilter& filter) {
    QVariantMap map;
    map.insert(QStringLiteral("query"), filter.query);
    map.insert(QStringLiteral("nameTerms"), filter.nameTerms);
    map.insert(QStringLiteral("excludeTerms"), filter.excludeTerms);
    map.insert(QStringLiteral("contentTerms"), filter.contentTerms);
    map.insert(QStringLiteral("searchPath"), filter.searchPath);
    map.insert(QStringLiteral("searchPaths"), filter.searchPaths);
    map.insert(QStringLiteral("thisPC"), filter.thisPC);
    map.insert(QStringLiteral("recursive"), filter.recursive);
    map.insert(QStringLiteral("caseSensitive"), filter.caseSensitive);
    map.insert(QStringLiteral("includeHidden"), filter.includeHidden);
    map.insert(QStringLiteral("searchContents"), filter.searchContents);
    map.insert(QStringLiteral("useIndex"), filter.useIndex);
    map.insert(QStringLiteral("kindFilter"), filter.kindFilter);
    map.insert(QStringLiteral("tags"), filter.tags);
    map.insert(QStringLiteral("extensions"), filter.extensions);
    map.insert(QStringLiteral("minSize"), filter.minSize);
    map.insert(QStringLiteral("maxSize"), filter.maxSize);
    map.insert(QStringLiteral("modifiedFrom"), filter.modifiedFrom);
    map.insert(QStringLiteral("modifiedTo"), filter.modifiedTo);
    return map;
}

SearchFilter filterFromMap(const QVariantMap& map) {
    SearchFilter filter = SearchQuery::parse(map.value(QStringLiteral("query")).toString());
    filter.nameTerms = map.value(QStringLiteral("nameTerms"), filter.nameTerms).toStringList();
    filter.excludeTerms =
        map.value(QStringLiteral("excludeTerms"), filter.excludeTerms).toStringList();
    filter.contentTerms =
        map.value(QStringLiteral("contentTerms"), filter.contentTerms).toStringList();
    filter.searchPath = map.value(QStringLiteral("searchPath")).toString();
    filter.searchPaths = map.value(QStringLiteral("searchPaths")).toStringList();
    filter.thisPC =
        map.value(QStringLiteral("thisPC"), map.value(QStringLiteral("thisMac"))).toBool();
    filter.recursive = map.value(QStringLiteral("recursive"), true).toBool();
    filter.caseSensitive = map.value(QStringLiteral("caseSensitive"), false).toBool();
    filter.includeHidden = map.value(QStringLiteral("includeHidden"), false).toBool();
    filter.searchContents = map.value(QStringLiteral("searchContents"), false).toBool();
    filter.useIndex = map.value(QStringLiteral("useIndex"), false).toBool();
    filter.kindFilter = map.value(QStringLiteral("kindFilter")).toString();
    filter.tags = map.value(QStringLiteral("tags")).toStringList();
    filter.extensions = map.value(QStringLiteral("extensions")).toStringList();
    filter.minSize = map.value(QStringLiteral("minSize"), -1).toLongLong();
    filter.maxSize = map.value(QStringLiteral("maxSize"), -1).toLongLong();
    filter.modifiedFrom = map.value(QStringLiteral("modifiedFrom")).toDate();
    filter.modifiedTo = map.value(QStringLiteral("modifiedTo")).toDate();
    return filter;
}

} // namespace

static std::unique_ptr<QSettings> createQSettings() {
    if (!s_customSettingsPath.isEmpty()) {
        return std::make_unique<QSettings>(s_customSettingsPath, QSettings::IniFormat);
    }
    return std::make_unique<QSettings>(QStringLiteral("FinderLinux"),
                                       QStringLiteral("FinderLinux"));
}

AppSettings::AppSettings() {
    m_defaultPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (m_defaultPath.isEmpty()) {
        m_defaultPath = QDir::homePath();
    }
    load();
}

void AppSettings::load() {
    auto sPtr = createQSettings();
    QSettings& s = *sPtr;
    m_viewMode = static_cast<ViewMode>(
        s.value(QStringLiteral("viewMode"), static_cast<int>(ViewMode::Icon)).toInt());
    m_showHiddenFiles = s.value(QStringLiteral("showHiddenFiles"), false).toBool();
    m_calculateFolderSizes = s.value(QStringLiteral("calculateFolderSizes"), false).toBool();
    m_directoriesFirst = s.value(QStringLiteral("directoriesFirst"), true).toBool();
    m_sortColumn = s.value(QStringLiteral("sortColumn"), 0).toInt();
    m_sortOrder = static_cast<Qt::SortOrder>(
        s.value(QStringLiteral("sortOrder"), static_cast<int>(Qt::AscendingOrder)).toInt());
    m_iconSize = s.value(QStringLiteral("iconSize"), 64).toInt();
    m_viewTextSize = s.value(QStringLiteral("viewTextSize"), 10).toInt();
    m_viewLabelPosition = s.value(QStringLiteral("viewLabelPosition"), 0).toInt();
    m_viewGridSpacing = s.value(QStringLiteral("viewGridSpacing"), 12).toInt();
    m_viewShowItemInfo = s.value(QStringLiteral("viewShowItemInfo"), false).toBool();
    m_viewShowIconPreview = s.value(QStringLiteral("viewShowIconPreview"), true).toBool();
    m_viewManualArrangement = s.value(QStringLiteral("viewManualArrangement"), false).toBool();
    m_folderViewOptions = s.value(QStringLiteral("folderViewOptions")).toMap();
    m_iconPositions = s.value(QStringLiteral("iconPositions")).toMap();
    m_smartFolders = s.value(QStringLiteral("smartFolders")).toList();
    m_recentFolders = s.value(QStringLiteral("recentFolders")).toStringList();
    m_recentServers = s.value(QStringLiteral("recentServers")).toStringList();
    m_listVisibleColumns =
        s.value(QStringLiteral("listVisibleColumns"), defaultListColumns()).toStringList();
    m_listColumnOrder =
        s.value(QStringLiteral("listColumnOrder"), defaultListColumns()).toStringList();
    m_listColumnWidths = s.value(QStringLiteral("listColumnWidths")).toMap();
    m_showSidebar = s.value(QStringLiteral("showSidebar"), true).toBool();
    m_showToolbar = s.value(QStringLiteral("showToolbar"), true).toBool();
    m_showPathBar = s.value(QStringLiteral("showPathBar"), true).toBool();
    m_showStatusBar = s.value(QStringLiteral("showStatusBar"), true).toBool();
    m_showPreviewPane = s.value(QStringLiteral("showPreviewPane"), false).toBool();
    m_theme = static_cast<AppTheme>(
        s.value(QStringLiteral("theme"), static_cast<int>(AppTheme::System)).toInt());
    m_customFavorites = s.value(QStringLiteral("customFavorites")).toStringList();
    m_favoriteOrder = s.value(QStringLiteral("favoriteOrder")).toStringList();
    m_hiddenFavorites = s.value(QStringLiteral("hiddenFavorites")).toStringList();
    m_hiddenLocations = s.value(QStringLiteral("hiddenLocations")).toStringList();
    m_locationOrder = s.value(QStringLiteral("locationOrder")).toStringList();
    m_windowGeometry = s.value(QStringLiteral("windowGeometry")).toByteArray();
    m_windowState = s.value(QStringLiteral("windowState")).toByteArray();
    m_springLoadedFolders = s.value(QStringLiteral("springLoadedFolders"), true).toBool();
    m_springLoadDelayMs = s.value(QStringLiteral("springLoadDelayMs"), 750).toInt();
    m_restoreTabsOnStartup = s.value(QStringLiteral("restoreTabsOnStartup"), true).toBool();
    m_savedTabs = s.value(QStringLiteral("savedTabs")).toStringList();
    m_rememberFolderViewModes = s.value(QStringLiteral("rememberFolderViewModes"), false).toBool();
    m_folderViewModes = s.value(QStringLiteral("folderViewModes")).toMap();
    m_columnViewWidths = s.value(QStringLiteral("columnViewWidths")).toMap();
    m_trashAutoPurgeDays = s.value(QStringLiteral("trashAutoPurgeDays"), 0).toInt();
}

void AppSettings::save() {
    auto sPtr = createQSettings();
    QSettings& s = *sPtr;
    s.setValue(QStringLiteral("viewMode"), static_cast<int>(m_viewMode));
    s.setValue(QStringLiteral("showHiddenFiles"), m_showHiddenFiles);
    s.setValue(QStringLiteral("calculateFolderSizes"), m_calculateFolderSizes);
    s.setValue(QStringLiteral("directoriesFirst"), m_directoriesFirst);
    s.setValue(QStringLiteral("sortColumn"), m_sortColumn);
    s.setValue(QStringLiteral("sortOrder"), static_cast<int>(m_sortOrder));
    s.setValue(QStringLiteral("iconSize"), m_iconSize);
    s.setValue(QStringLiteral("viewTextSize"), m_viewTextSize);
    s.setValue(QStringLiteral("viewLabelPosition"), m_viewLabelPosition);
    s.setValue(QStringLiteral("viewGridSpacing"), m_viewGridSpacing);
    s.setValue(QStringLiteral("viewShowItemInfo"), m_viewShowItemInfo);
    s.setValue(QStringLiteral("viewShowIconPreview"), m_viewShowIconPreview);
    s.setValue(QStringLiteral("viewManualArrangement"), m_viewManualArrangement);
    s.setValue(QStringLiteral("folderViewOptions"), m_folderViewOptions);
    s.setValue(QStringLiteral("iconPositions"), m_iconPositions);
    s.setValue(QStringLiteral("smartFolders"), m_smartFolders);
    s.setValue(QStringLiteral("recentFolders"), m_recentFolders);
    s.setValue(QStringLiteral("recentServers"), m_recentServers);
    s.setValue(QStringLiteral("listVisibleColumns"), m_listVisibleColumns);
    s.setValue(QStringLiteral("listColumnOrder"), m_listColumnOrder);
    s.setValue(QStringLiteral("listColumnWidths"), m_listColumnWidths);
    s.setValue(QStringLiteral("showSidebar"), m_showSidebar);
    s.setValue(QStringLiteral("showToolbar"), m_showToolbar);
    s.setValue(QStringLiteral("showPathBar"), m_showPathBar);
    s.setValue(QStringLiteral("showStatusBar"), m_showStatusBar);
    s.setValue(QStringLiteral("showPreviewPane"), m_showPreviewPane);
    s.setValue(QStringLiteral("theme"), static_cast<int>(m_theme));
    s.setValue(QStringLiteral("customFavorites"), m_customFavorites);
    s.setValue(QStringLiteral("favoriteOrder"), m_favoriteOrder);
    s.setValue(QStringLiteral("hiddenFavorites"), m_hiddenFavorites);
    s.setValue(QStringLiteral("hiddenLocations"), m_hiddenLocations);
    s.setValue(QStringLiteral("locationOrder"), m_locationOrder);
    s.setValue(QStringLiteral("windowGeometry"), m_windowGeometry);
    s.setValue(QStringLiteral("windowState"), m_windowState);
    s.setValue(QStringLiteral("springLoadedFolders"), m_springLoadedFolders);
    s.setValue(QStringLiteral("springLoadDelayMs"), m_springLoadDelayMs);
    s.setValue(QStringLiteral("restoreTabsOnStartup"), m_restoreTabsOnStartup);
    s.setValue(QStringLiteral("savedTabs"), m_savedTabs);
    s.setValue(QStringLiteral("rememberFolderViewModes"), m_rememberFolderViewModes);
    s.setValue(QStringLiteral("folderViewModes"), m_folderViewModes);
    s.setValue(QStringLiteral("columnViewWidths"), m_columnViewWidths);
    s.setValue(QStringLiteral("trashAutoPurgeDays"), m_trashAutoPurgeDays);
}

std::optional<ViewMode> AppSettings::folderViewMode(const QString& path) const {
    if (!m_rememberFolderViewModes || path.isEmpty())
        return std::nullopt;
    auto it = m_folderViewModes.find(path);
    if (it != m_folderViewModes.end()) {
        return static_cast<ViewMode>(it.value().toInt());
    }
    return std::nullopt;
}

void AppSettings::setFolderViewMode(const QString& path, ViewMode mode) {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")))
        return;
    m_folderViewModes.insert(path, static_cast<int>(mode));
    save();
}

QStringList AppSettings::defaultListColumns() {
    return {QStringLiteral("name"), QStringLiteral("dateModified"), QStringLiteral("size"),
            QStringLiteral("kind")};
}

FolderViewOptions AppSettings::defaultViewOptions() const {
    FolderViewOptions options;
    options.iconSize = m_iconSize;
    options.textSize = m_viewTextSize;
    options.labelPosition = m_viewLabelPosition;
    options.gridSpacing = m_viewGridSpacing;
    options.showItemInfo = m_viewShowItemInfo;
    options.showIconPreview = m_viewShowIconPreview;
    options.manualArrangement = m_viewManualArrangement;
    return options;
}

void AppSettings::setDefaultViewOptions(const FolderViewOptions& options) {
    m_iconSize = qBound(16, options.iconSize, 256);
    m_viewTextSize = qBound(8, options.textSize, 24);
    m_viewLabelPosition = (options.labelPosition == 1) ? 1 : 0;
    m_viewGridSpacing = qBound(0, options.gridSpacing, 48);
    m_viewShowItemInfo = options.showItemInfo;
    m_viewShowIconPreview = options.showIconPreview;
    m_viewManualArrangement = options.manualArrangement;
    save();
}

FolderViewOptions AppSettings::viewOptions(const QString& path) const {
    FolderViewOptions options = defaultViewOptions();
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://"))) {
        return options;
    }

    const auto it = m_folderViewOptions.constFind(path);
    if (it == m_folderViewOptions.constEnd()) {
        return options;
    }

    const QVariantList values = it.value().toList();
    if (values.size() >= 6) {
        options.iconSize = qBound(16, values.at(0).toInt(), 256);
        options.textSize = qBound(8, values.at(1).toInt(), 24);
        options.labelPosition = (values.at(2).toInt() == 1) ? 1 : 0;
        options.gridSpacing = qBound(0, values.at(3).toInt(), 48);
        options.showItemInfo = values.at(4).toBool();
        options.showIconPreview = values.at(5).toBool();
        if (values.size() >= 7) {
            options.manualArrangement = values.at(6).toBool();
        }
    }
    return options;
}

void AppSettings::setViewOptions(const QString& path, const FolderViewOptions& options) {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")))
        return;
    QVariantList values;
    values << qBound(16, options.iconSize, 256) << qBound(8, options.textSize, 24)
           << ((options.labelPosition == 1) ? 1 : 0) << qBound(0, options.gridSpacing, 48)
           << options.showItemInfo << options.showIconPreview << options.manualArrangement;
    m_folderViewOptions.insert(path, values);
    save();
}

void AppSettings::clearViewOptions(const QString& path) {
    if (m_folderViewOptions.remove(path) > 0) {
        save();
    }
}

QVariantMap AppSettings::iconPositions(const QString& path) const {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")))
        return {};
    return m_iconPositions.value(path).toMap();
}

void AppSettings::setIconPositions(const QString& path, const QVariantMap& positions) {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")))
        return;
    if (positions.isEmpty()) {
        clearIconPositions(path);
        return;
    }
    m_iconPositions.insert(path, positions);
    save();
}

void AppSettings::clearIconPositions(const QString& path) {
    if (m_iconPositions.remove(path) > 0) {
        save();
    }
}

void AppSettings::addRecentFolder(const QString& path) {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")) ||
        path.startsWith(QStringLiteral("smart://")) ||
        path.startsWith(QStringLiteral("recent://")) ||
        path.startsWith(QStringLiteral("apps://"))) {
        return;
    }
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty())
        return;
    m_recentFolders.removeAll(clean);
    m_recentFolders.prepend(clean);
    while (m_recentFolders.size() > 15)
        m_recentFolders.removeLast();
    save();
}

void AppSettings::clearRecentFolders() {
    if (m_recentFolders.isEmpty())
        return;
    m_recentFolders.clear();
    save();
}

void AppSettings::addRecentServer(const QString& uri) {
    if (uri.trimmed().isEmpty())
        return;
    const QString clean = uri.trimmed();
    m_recentServers.removeAll(clean);
    m_recentServers.prepend(clean);
    while (m_recentServers.size() > 10)
        m_recentServers.removeLast();
    save();
}

void AppSettings::clearRecentServers() {
    if (m_recentServers.isEmpty())
        return;
    m_recentServers.clear();
    save();
}

QList<SmartFolder> AppSettings::smartFolders() const {
    QList<SmartFolder> folders;
    folders.reserve(m_smartFolders.size());
    for (const QVariant& value : m_smartFolders) {
        const QVariantMap map = value.toMap();
        SmartFolder folder;
        folder.name = map.value(QStringLiteral("name")).toString();
        folder.filter = filterFromMap(map.value(QStringLiteral("filter")).toMap());
        if (folder.isValid()) {
            folders.append(folder);
        }
    }
    return folders;
}

SmartFolder AppSettings::smartFolder(const QString& name) const {
    for (const SmartFolder& folder : smartFolders()) {
        if (folder.name.compare(name, Qt::CaseInsensitive) == 0) {
            return folder;
        }
    }
    return {};
}

void AppSettings::saveSmartFolder(const SmartFolder& folder) {
    if (!folder.isValid())
        return;

    QVariantList updated;
    updated.reserve(m_smartFolders.size() + 1);
    bool replaced = false;
    for (const QVariant& value : m_smartFolders) {
        const QVariantMap existing = value.toMap();
        if (existing.value(QStringLiteral("name"))
                .toString()
                .compare(folder.name, Qt::CaseInsensitive) == 0) {
            QVariantMap map;
            map.insert(QStringLiteral("name"), folder.name);
            map.insert(QStringLiteral("filter"), filterToMap(folder.filter));
            updated.append(map);
            replaced = true;
        } else {
            updated.append(existing);
        }
    }
    if (!replaced) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), folder.name);
        map.insert(QStringLiteral("filter"), filterToMap(folder.filter));
        updated.append(map);
    }
    m_smartFolders = updated;
    save();
}

void AppSettings::removeSmartFolder(const QString& name) {
    QVariantList updated;
    for (const QVariant& value : m_smartFolders) {
        const QVariantMap existing = value.toMap();
        if (existing.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) !=
            0) {
            updated.append(existing);
        }
    }
    if (updated.size() != m_smartFolders.size()) {
        m_smartFolders = updated;
        save();
    }
}

void AppSettings::setListVisibleColumns(const QStringList& columns) {
    m_listVisibleColumns = columns;
    save();
}

void AppSettings::setListColumnOrder(const QStringList& order) {
    m_listColumnOrder = order;
    save();
}

void AppSettings::setListColumnWidths(const QVariantMap& widths) {
    m_listColumnWidths = widths;
    save();
}

int AppSettings::columnViewWidth(const QString& path) const {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")))
        return 0;
    const auto it = m_columnViewWidths.constFind(path);
    return it == m_columnViewWidths.constEnd() ? 0 : it.value().toInt();
}

void AppSettings::setColumnViewWidth(const QString& path, int width) {
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")) || width <= 0)
        return;
    m_columnViewWidths.insert(path, width);
    save();
}

void AppSettings::removeColumnViewWidth(const QString& path) {
    if (path.isEmpty())
        return;
    if (m_columnViewWidths.remove(path) > 0) {
        save();
    }
}

void AppSettings::addFavorite(const QString& path) {
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

void AppSettings::removeFavorite(const QString& path) {
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

QStringList AppSettings::defaultFavorites() {
    QStringList defaults;
    defaults << QDir::homePath();
    auto addStd = [&](QStandardPaths::StandardLocation loc) {
        QString p = QStandardPaths::writableLocation(loc);
        if (!p.isEmpty() && !defaults.contains(p))
            defaults << p;
    };
    addStd(QStandardPaths::DesktopLocation);
    addStd(QStandardPaths::DocumentsLocation);
    addStd(QStandardPaths::DownloadLocation);
    addStd(QStandardPaths::PicturesLocation);
    addStd(QStandardPaths::MusicLocation);
    addStd(QStandardPaths::MoviesLocation);
    return defaults;
}

QStringList AppSettings::favoriteOrder() const {
    const QStringList defaults = defaultFavorites();
    QStringList allKnown = defaults;
    for (const auto& fav : m_customFavorites) {
        if (!allKnown.contains(fav))
            allKnown.append(fav);
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

void AppSettings::setFavoriteOrder(const QStringList& order) {
    m_favoriteOrder = order;
    save();
}

void AppSettings::reorderFavorite(const QString& path, int newIndex) {
    QStringList order = favoriteOrder();
    if (!order.contains(path)) {
        order.append(path);
    }
    order.removeAll(path);
    int clampedIndex = qBound(0, newIndex, static_cast<int>(order.size()));
    order.insert(clampedIndex, path);
    setFavoriteOrder(order);
}

void AppSettings::moveFavoriteUp(const QString& path) {
    QStringList order = favoriteOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx > 0) {
        order.swapItemsAt(idx, idx - 1);
        setFavoriteOrder(order);
    }
}

void AppSettings::moveFavoriteDown(const QString& path) {
    QStringList order = favoriteOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx >= 0 && idx < static_cast<int>(order.size()) - 1) {
        order.swapItemsAt(idx, idx + 1);
        setFavoriteOrder(order);
    }
}

void AppSettings::moveFavoriteToTop(const QString& path) {
    QStringList order = favoriteOrder();
    order.removeAll(path);
    order.prepend(path);
    setFavoriteOrder(order);
}

void AppSettings::moveFavoriteToBottom(const QString& path) {
    QStringList order = favoriteOrder();
    order.removeAll(path);
    order.append(path);
    setFavoriteOrder(order);
}

void AppSettings::setHiddenFavorites(const QStringList& list) {
    m_hiddenFavorites = list;
    save();
}

void AppSettings::hideFavorite(const QString& path) {
    if (!m_hiddenFavorites.contains(path)) {
        m_hiddenFavorites.append(path);
        save();
    }
}

void AppSettings::unhideFavorite(const QString& path) {
    if (m_hiddenFavorites.removeAll(path) > 0) {
        save();
    }
}

void AppSettings::unhideAllFavorites() {
    if (!m_hiddenFavorites.isEmpty()) {
        m_hiddenFavorites.clear();
        save();
    }
}

void AppSettings::setHiddenLocations(const QStringList& list) {
    m_hiddenLocations = list;
    save();
}

void AppSettings::hideLocation(const QString& pathOrName) {
    if (!m_hiddenLocations.contains(pathOrName)) {
        m_hiddenLocations.append(pathOrName);
        save();
    }
}

void AppSettings::unhideLocation(const QString& pathOrName) {
    if (m_hiddenLocations.removeAll(pathOrName) > 0) {
        save();
    }
}

void AppSettings::unhideAllLocations() {
    if (!m_hiddenLocations.isEmpty()) {
        m_hiddenLocations.clear();
        save();
    }
}

void AppSettings::resetSidebarDefaults() {
    m_favoriteOrder.clear();
    m_locationOrder.clear();
    m_hiddenFavorites.clear();
    m_hiddenLocations.clear();
    save();
}

QStringList AppSettings::locationOrder() const {
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

void AppSettings::setLocationOrder(const QStringList& order) {
    m_locationOrder = order;
    save();
}

void AppSettings::reorderLocation(const QString& path, int newIndex) {
    QStringList order = locationOrder();
    if (!order.contains(path)) {
        order.append(path);
    }
    order.removeAll(path);
    int clampedIndex = qBound(0, newIndex, static_cast<int>(order.size()));
    order.insert(clampedIndex, path);
    setLocationOrder(order);
}

void AppSettings::moveLocationUp(const QString& path) {
    QStringList order = locationOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx > 0) {
        order.swapItemsAt(idx, idx - 1);
        setLocationOrder(order);
    }
}

void AppSettings::moveLocationDown(const QString& path) {
    QStringList order = locationOrder();
    int idx = static_cast<int>(order.indexOf(path));
    if (idx >= 0 && idx < static_cast<int>(order.size()) - 1) {
        order.swapItemsAt(idx, idx + 1);
        setLocationOrder(order);
    }
}

void AppSettings::moveLocationToTop(const QString& path) {
    QStringList order = locationOrder();
    order.removeAll(path);
    order.prepend(path);
    setLocationOrder(order);
}

void AppSettings::moveLocationToBottom(const QString& path) {
    QStringList order = locationOrder();
    order.removeAll(path);
    order.append(path);
    setLocationOrder(order);
}
