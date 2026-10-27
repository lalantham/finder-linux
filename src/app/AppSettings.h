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
    void setShowHiddenFiles(bool show) { m_showHiddenFiles = show; }

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
    AppTheme m_theme{AppTheme::Light};
    QString m_defaultPath;
    QStringList m_customFavorites;
    QByteArray m_windowGeometry;
    QByteArray m_windowState;
};
