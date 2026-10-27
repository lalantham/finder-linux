#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QSettings>

enum class ViewMode {
    Icon = 0,
    List = 1,
    Column = 2,
    Gallery = 3
};

enum class AppTheme {
    System = 0,
    Light = 1,
    Dark = 2
};

class AppSettings {
public:
    static AppSettings& instance();

    void load();
    void save();

    [[nodiscard]] ViewMode viewMode() const noexcept { return m_viewMode; }
    void setViewMode(ViewMode mode) { m_viewMode = mode; }

    [[nodiscard]] bool showHiddenFiles() const noexcept { return m_showHiddenFiles; }
    void setShowHiddenFiles(bool show) {
        m_showHiddenFiles = show;
        save();
    }

    [[nodiscard]] bool directoriesFirst() const noexcept { return m_directoriesFirst; }
    void setDirectoriesFirst(bool dirsFirst) { m_directoriesFirst = dirsFirst; }

    [[nodiscard]] int iconSize() const noexcept { return m_iconSize; }
    void setIconSize(int size) { m_iconSize = size; }

    [[nodiscard]] AppTheme theme() const noexcept { return m_theme; }
    void setTheme(AppTheme theme) { m_theme = theme; }

    [[nodiscard]] QString defaultPath() const noexcept { return m_defaultPath; }
    void setDefaultPath(const QString& path) { m_defaultPath = path; }

    [[nodiscard]] QStringList customFavorites() const noexcept { return m_customFavorites; }
    void setCustomFavorites(const QStringList& favs) { m_customFavorites = favs; }
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

    [[nodiscard]] QStringList hiddenFavorites() const noexcept { return m_hiddenFavorites; }
    void setHiddenFavorites(const QStringList& list);
    void hideFavorite(const QString& path);
    void unhideFavorite(const QString& path);
    void unhideAllFavorites();
    [[nodiscard]] bool isFavoriteHidden(const QString& path) const { return m_hiddenFavorites.contains(path); }

    [[nodiscard]] QStringList hiddenLocations() const noexcept { return m_hiddenLocations; }
    void setHiddenLocations(const QStringList& list);
    void hideLocation(const QString& pathOrName);
    void unhideLocation(const QString& pathOrName);
    void unhideAllLocations();
    [[nodiscard]] bool isLocationHidden(const QString& pathOrName) const { return m_hiddenLocations.contains(pathOrName); }

    [[nodiscard]] QStringList locationOrder() const;
    void setLocationOrder(const QStringList& order);
    void reorderLocation(const QString& path, int newIndex);
    void moveLocationUp(const QString& path);
    void moveLocationDown(const QString& path);
    void moveLocationToTop(const QString& path);
    void moveLocationToBottom(const QString& path);

    void resetSidebarDefaults();

    [[nodiscard]] QByteArray windowGeometry() const noexcept { return m_windowGeometry; }
    void setWindowGeometry(const QByteArray& geom) { m_windowGeometry = geom; }

    [[nodiscard]] QByteArray windowState() const noexcept { return m_windowState; }
    void setWindowState(const QByteArray& state) { m_windowState = state; }

private:
    AppSettings();

    ViewMode m_viewMode{ViewMode::Icon};
    bool m_showHiddenFiles{false};
    bool m_directoriesFirst{true};
    int m_iconSize{64};
    AppTheme m_theme{AppTheme::System};
    QString m_defaultPath;
    QStringList m_customFavorites;
    QStringList m_favoriteOrder;
    QStringList m_hiddenFavorites;
    QStringList m_hiddenLocations;
    QStringList m_locationOrder;
    QByteArray m_windowGeometry;
    QByteArray m_windowState;
};
