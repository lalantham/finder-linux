#pragma once

#include "SearchQuery.h"
#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <Qt>
#include <optional>

enum class ViewMode { Icon = 0, List = 1, Column = 2, Gallery = 3 };

enum class AppTheme { System = 0, Light = 1, Dark = 2 };

// Per-folder presentation options.
struct FolderViewOptions {
    int iconSize{64};
    int textSize{10};
    int labelPosition{0}; // 0 = below icon, 1 = to the right of icon
    int gridSpacing{12};
    bool showItemInfo{false};
    bool showIconPreview{true};
    bool manualArrangement{false};

    bool operator==(const FolderViewOptions&) const = default;
};

class AppSettings {
public:
    static AppSettings& instance();
    static void setSettingsFilePath(const QString& path);

    void load();
    void save();

    [[nodiscard]] ViewMode viewMode() const noexcept {
        return m_viewMode;
    }
    void setViewMode(ViewMode mode) {
        m_viewMode = mode;
    }

    [[nodiscard]] bool rememberFolderViewModes() const noexcept {
        return m_rememberFolderViewModes;
    }
    void setRememberFolderViewModes(bool enable) {
        m_rememberFolderViewModes = enable;
    }
    [[nodiscard]] std::optional<ViewMode> folderViewMode(const QString& path) const;
    void setFolderViewMode(const QString& path, ViewMode mode);

    [[nodiscard]] int columnViewWidth(const QString& path) const;
    void setColumnViewWidth(const QString& path, int width);
    void removeColumnViewWidth(const QString& path);

    [[nodiscard]] int trashAutoPurgeDays() const noexcept {
        return m_trashAutoPurgeDays;
    }
    void setTrashAutoPurgeDays(int days) {
        m_trashAutoPurgeDays = qMax(0, days);
    }

    [[nodiscard]] bool showHiddenFiles() const noexcept {
        return m_showHiddenFiles;
    }
    void setShowHiddenFiles(bool show) {
        m_showHiddenFiles = show;
        save();
    }

    [[nodiscard]] bool calculateFolderSizes() const noexcept {
        return m_calculateFolderSizes;
    }
    void setCalculateFolderSizes(bool calc) {
        m_calculateFolderSizes = calc;
        save();
    }

    [[nodiscard]] bool directoriesFirst() const noexcept {
        return m_directoriesFirst;
    }
    void setDirectoriesFirst(bool dirsFirst) {
        m_directoriesFirst = dirsFirst;
    }

    [[nodiscard]] int sortColumn() const noexcept {
        return m_sortColumn;
    }
    void setSortColumn(int column) {
        m_sortColumn = column;
    }
    [[nodiscard]] Qt::SortOrder sortOrder() const noexcept {
        return m_sortOrder;
    }
    void setSortOrder(Qt::SortOrder order) {
        m_sortOrder = order;
    }

    [[nodiscard]] int iconSize() const noexcept {
        return m_iconSize;
    }
    void setIconSize(int size) {
        m_iconSize = size;
    }

    [[nodiscard]] FolderViewOptions defaultViewOptions() const;
    void setDefaultViewOptions(const FolderViewOptions& options);
    [[nodiscard]] FolderViewOptions viewOptions(const QString& path) const;
    void setViewOptions(const QString& path, const FolderViewOptions& options);
    void clearViewOptions(const QString& path);
    [[nodiscard]] QVariantMap iconPositions(const QString& path) const;
    void setIconPositions(const QString& path, const QVariantMap& positions);
    void clearIconPositions(const QString& path);

    [[nodiscard]] QStringList recentFolders() const noexcept {
        return m_recentFolders;
    }
    void addRecentFolder(const QString& path);
    void clearRecentFolders();
    [[nodiscard]] QStringList recentServers() const noexcept {
        return m_recentServers;
    }
    void addRecentServer(const QString& uri);
    void clearRecentServers();

    [[nodiscard]] QList<SmartFolder> smartFolders() const;
    [[nodiscard]] SmartFolder smartFolder(const QString& name) const;
    void saveSmartFolder(const SmartFolder& folder);
    void removeSmartFolder(const QString& name);

    static QStringList defaultListColumns();
    [[nodiscard]] QStringList listVisibleColumns() const noexcept {
        return m_listVisibleColumns;
    }
    void setListVisibleColumns(const QStringList& columns);
    [[nodiscard]] QStringList listColumnOrder() const noexcept {
        return m_listColumnOrder;
    }
    void setListColumnOrder(const QStringList& order);
    [[nodiscard]] QVariantMap listColumnWidths() const noexcept {
        return m_listColumnWidths;
    }
    void setListColumnWidths(const QVariantMap& widths);

    [[nodiscard]] bool showSidebar() const noexcept {
        return m_showSidebar;
    }
    void setShowSidebar(bool show) {
        m_showSidebar = show;
    }
    [[nodiscard]] bool showToolbar() const noexcept {
        return m_showToolbar;
    }
    void setShowToolbar(bool show) {
        m_showToolbar = show;
    }
    [[nodiscard]] bool showPathBar() const noexcept {
        return m_showPathBar;
    }
    void setShowPathBar(bool show) {
        m_showPathBar = show;
    }
    [[nodiscard]] bool showStatusBar() const noexcept {
        return m_showStatusBar;
    }
    void setShowStatusBar(bool show) {
        m_showStatusBar = show;
    }
    [[nodiscard]] bool showPreviewPane() const noexcept {
        return m_showPreviewPane;
    }
    void setShowPreviewPane(bool show) {
        m_showPreviewPane = show;
    }
    [[nodiscard]] bool useNativeTitleBar() const noexcept {
        return m_useNativeTitleBar;
    }
    void setUseNativeTitleBar(bool use) {
        m_useNativeTitleBar = use;
    }

    [[nodiscard]] AppTheme theme() const noexcept {
        return m_theme;
    }
    void setTheme(AppTheme theme) {
        m_theme = theme;
    }

    [[nodiscard]] QString defaultPath() const noexcept {
        return m_defaultPath;
    }
    void setDefaultPath(const QString& path) {
        m_defaultPath = path;
    }

    [[nodiscard]] QStringList customFavorites() const noexcept {
        return m_customFavorites;
    }
    void setCustomFavorites(const QStringList& favs) {
        m_customFavorites = favs;
    }
    void addFavorite(const QString& path);
    void removeFavorite(const QString& path);

    static QStringList defaultFavorites();
    [[nodiscard]] QStringList favoriteOrder() const;
    void setFavoriteOrder(const QStringList& order);
    void reorderFavorite(const QString& path, int newIndex);
    void moveFavoriteUp(const QString& path);
    void moveFavoriteDown(const QString& path);
    void moveFavoriteToTop(const QString& path);
    void moveFavoriteToBottom(const QString& path);

    [[nodiscard]] QStringList hiddenFavorites() const noexcept {
        return m_hiddenFavorites;
    }
    void setHiddenFavorites(const QStringList& list);
    void hideFavorite(const QString& path);
    void unhideFavorite(const QString& path);
    void unhideAllFavorites();
    [[nodiscard]] bool isFavoriteHidden(const QString& path) const {
        return m_hiddenFavorites.contains(path);
    }

    [[nodiscard]] QStringList hiddenLocations() const noexcept {
        return m_hiddenLocations;
    }
    void setHiddenLocations(const QStringList& list);
    void hideLocation(const QString& pathOrName);
    void unhideLocation(const QString& pathOrName);
    void unhideAllLocations();
    [[nodiscard]] bool isLocationHidden(const QString& pathOrName) const {
        return m_hiddenLocations.contains(pathOrName);
    }

    [[nodiscard]] QStringList locationOrder() const;
    void setLocationOrder(const QStringList& order);
    void reorderLocation(const QString& path, int newIndex);
    void moveLocationUp(const QString& path);
    void moveLocationDown(const QString& path);
    void moveLocationToTop(const QString& path);
    void moveLocationToBottom(const QString& path);

    void resetSidebarDefaults();

    [[nodiscard]] bool springLoadedFoldersEnabled() const noexcept {
        return m_springLoadedFolders;
    }
    void setSpringLoadedFoldersEnabled(bool enabled) {
        m_springLoadedFolders = enabled;
    }
    [[nodiscard]] int springLoadDelayMs() const noexcept {
        return m_springLoadDelayMs;
    }
    void setSpringLoadDelayMs(int delayMs) {
        m_springLoadDelayMs = delayMs;
    }

    [[nodiscard]] QByteArray windowGeometry() const noexcept {
        return m_windowGeometry;
    }
    void setWindowGeometry(const QByteArray& geom) {
        m_windowGeometry = geom;
    }

    [[nodiscard]] QByteArray windowState() const noexcept {
        return m_windowState;
    }
    void setWindowState(const QByteArray& state) {
        m_windowState = state;
    }

    [[nodiscard]] bool restoreTabsOnStartup() const noexcept {
        return m_restoreTabsOnStartup;
    }
    void setRestoreTabsOnStartup(bool enable) {
        m_restoreTabsOnStartup = enable;
    }

    [[nodiscard]] QStringList savedTabs() const noexcept {
        return m_savedTabs;
    }
    void setSavedTabs(const QStringList& tabs) {
        m_savedTabs = tabs;
    }

private:
    AppSettings();

    ViewMode m_viewMode{ViewMode::Icon};
    bool m_showHiddenFiles{false};
    bool m_calculateFolderSizes{false};
    bool m_directoriesFirst{true};
    int m_sortColumn{0};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
    int m_iconSize{64};
    int m_viewTextSize{10};
    int m_viewLabelPosition{0};
    int m_viewGridSpacing{12};
    bool m_viewShowItemInfo{false};
    bool m_viewShowIconPreview{true};
    bool m_viewManualArrangement{false};
    QVariantMap m_folderViewOptions;
    QVariantMap m_iconPositions;
    QVariantList m_smartFolders;
    QStringList m_recentFolders;
    QStringList m_recentServers;
    QStringList m_listVisibleColumns;
    QStringList m_listColumnOrder;
    QVariantMap m_listColumnWidths;
    bool m_showSidebar{true};
    bool m_showToolbar{true};
    bool m_showPathBar{true};
    bool m_showStatusBar{true};
    bool m_showPreviewPane{false};
    bool m_useNativeTitleBar{false};
    AppTheme m_theme{AppTheme::System};
    QString m_defaultPath;
    QStringList m_customFavorites;
    QStringList m_favoriteOrder;
    QStringList m_hiddenFavorites;
    QStringList m_hiddenLocations;
    QStringList m_locationOrder;
    QByteArray m_windowGeometry;
    QByteArray m_windowState;
    bool m_springLoadedFolders{true};
    int m_springLoadDelayMs{750};
    bool m_restoreTabsOnStartup{true};
    QStringList m_savedTabs;
    bool m_rememberFolderViewModes{false};
    QVariantMap m_folderViewModes;
    QVariantMap m_columnViewWidths;
    int m_trashAutoPurgeDays{0};
};
