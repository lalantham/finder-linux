#pragma once

#include "AppSettings.h"
#include "ColumnView.h"
#include "FileSystemModel.h"
#include "GalleryView.h"
#include "IconView.h"
#include "ListView.h"
#include "SearchEngine.h"
#include "SearchScopeBar.h"
#include <QStackedWidget>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QAbstractItemView;
class QSplitter;
class PreviewPane;

class TabPage : public QWidget {
    Q_OBJECT

public:
    explicit TabPage(const QString& initialPath, QWidget* parent = nullptr);

    void navigateTo(const QString& path, bool recordHistory = true);
    void goBack();
    void goForward();
    void goUp();
    void refresh(const QString& pathToSelect = QString());
    void refreshWithSelection(const QStringList& pathsToSelect);
    void revealFile(const QString& filePath);
    void showTaggedFiles(const QString& tag, bool recordHistory = true);
    void showSmartFolder(const SmartFolder& folder, bool recordHistory = true);
    void showSmartFolder(const QString& name, bool recordHistory = true);
    void showRecentFiles(bool recordHistory = true);

    [[nodiscard]] QString searchText() const noexcept {
        return m_searchQuery;
    }
    [[nodiscard]] SearchFilter currentSearchFilter() const;

    [[nodiscard]] QString currentPath() const noexcept;
    [[nodiscard]] bool canGoBack() const noexcept {
        return !m_backHistory.isEmpty();
    }
    [[nodiscard]] bool canGoForward() const noexcept {
        return !m_forwardHistory.isEmpty();
    }

    void setViewMode(ViewMode mode);
    [[nodiscard]] ViewMode viewMode() const noexcept {
        return m_viewMode;
    }

    void applySort(int column, Qt::SortOrder order);
    [[nodiscard]] int sortColumn() const;
    [[nodiscard]] Qt::SortOrder sortOrder() const;
    void setDirectoriesFirst(bool dirsFirst);
    void applyIconSize(int size);
    void applyViewOptions();
    void cleanUpIconView();

    void setShowHidden(bool show);
    void setCalculateFolderSizes(bool enabled);
    void setFilterQuery(const QString& query);
    void setPreviewPaneVisible(bool visible);
    [[nodiscard]] bool isPreviewPaneVisible() const noexcept {
        return m_previewPaneVisible;
    }

    [[nodiscard]] FileSystemModel* model() const noexcept {
        return m_model;
    }
    [[nodiscard]] QString selectedFilePath() const;
    [[nodiscard]] QStringList selectedFilePaths() const;
    void selectAll();
    void invertSelection();
    void selectByPattern(const QString& pattern);
    bool triggerRename();
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept {
        return m_isActive;
    }

signals:
    void activated(TabPage* page);
    void pathChanged(const QString& newPath);
    void historyChanged();
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void statusMessageChanged(const QString& message);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                              const QString& currentDir);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory,
                           Qt::DropAction action);
    void searchClearRequested();
    void searchQueryEdited(const QString& query);
    void saveSearchRequested();
    void itemRenamed(const QString& oldPath, const QString& newPath);

public slots:
    void updateThemeStyles(bool isDark);

private slots:
    void onDirectoryLoaded(const QString& path, int itemCount);
    void runSearch();
    void onSearchBatchTimer();
    void onSearchItemsFound(const QVector<FileItem>& items);
    void onSearchFinished(int total);
    void onLiveSearchTimer();
    void onRefineTimer();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateStatus();
    [[nodiscard]] QAbstractItemView* currentView() const;
    void updatePreviewPane();
    void applyPreviewPaneVisibility();
    [[nodiscard]] SearchFilter buildSearchFilter() const;
    void clearSmartFolderState();
    void applyPendingSelection();
    void sortSearchResults(const QString& mode);

    bool m_isActive{true};
    QString m_currentPath;
    QString m_searchQuery;
    QString m_pendingSelection;
    QStringList m_pendingSelections;
    QVector<QString> m_backHistory;
    QVector<QString> m_forwardHistory;

    FileSystemModel* m_model{nullptr};
    ViewMode m_viewMode{ViewMode::Icon};
    ViewMode m_preSearchViewMode{ViewMode::Icon};

    SearchScopeBar* m_searchScopeBar{nullptr};
    SearchEngine* m_searchEngine{nullptr};
    QTimer* m_searchBatchTimer{nullptr};
    QTimer* m_filterDebounceTimer{nullptr};
    QTimer* m_previewDebounceTimer{nullptr};
    QTimer* m_liveSearchTimer{nullptr};
    QTimer* m_refineDebounceTimer{nullptr};
    QVector<FileItem> m_pendingSearchResults;
    SmartFolder m_activeSmartFolder;
    bool m_isSmartFolderView{false};

    QSplitter* m_viewSplitter{nullptr};
    QStackedWidget* m_viewStack{nullptr};
    PreviewPane* m_previewPane{nullptr};
    bool m_previewPaneVisible{false};
    IconView* m_iconView{nullptr};
    ListView* m_listView{nullptr};
    ColumnView* m_columnView{nullptr};
    GalleryView* m_galleryView{nullptr};
};
