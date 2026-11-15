#include "TabPage.h"
#include "PreviewPane.h"
#include "RecentFilesManager.h"
#include <QAbstractItemView>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSplitter>
#include <QStyle>
#include <QVBoxLayout>

TabPage::TabPage(const QString& initialPath, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("finderTabPage"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_model = new FileSystemModel(this);
    m_model->setShowHidden(AppSettings::instance().showHiddenFiles());
    m_model->setDirectoriesFirst(AppSettings::instance().directoriesFirst());
    connect(m_model, &FileSystemModel::sortChanged, this, [](int column, Qt::SortOrder order) {
        AppSettings::instance().setSortColumn(column);
        AppSettings::instance().setSortOrder(order);
        AppSettings::instance().save();
    });
    m_model->sort(AppSettings::instance().sortColumn(), AppSettings::instance().sortOrder());

    m_viewStack = new QStackedWidget(this);

    m_iconView = new IconView(m_viewStack);
    m_iconView->setFileSystemModel(m_model);
    m_iconView->setIconSizeValue(AppSettings::instance().iconSize());

    m_listView = new ListView(m_viewStack);
    m_listView->setFileSystemModel(m_model);

    m_columnView = new ColumnView(m_viewStack);

    m_galleryView = new GalleryView(m_viewStack);
    m_galleryView->setFileSystemModel(m_model);

    m_viewStack->addWidget(m_iconView);
    m_viewStack->addWidget(m_listView);
    m_viewStack->addWidget(m_columnView);
    m_viewStack->addWidget(m_galleryView);

    m_searchScopeBar = new SearchScopeBar(this);
    m_searchScopeBar->setIndexProvider(SearchEngine::availableIndexProvider());
    m_searchScopeBar->hide();
    layout->addWidget(m_searchScopeBar);

    m_viewSplitter = new QSplitter(Qt::Horizontal, this);
    m_viewSplitter->setHandleWidth(1);
    m_viewSplitter->addWidget(m_viewStack);

    m_previewPane = new PreviewPane(m_viewSplitter);
    m_previewPane->setMinimumWidth(220);
    m_viewSplitter->addWidget(m_previewPane);
    m_viewSplitter->setStretchFactor(0, 1);
    m_viewSplitter->setStretchFactor(1, 0);
    layout->addWidget(m_viewSplitter, 1);

    connect(m_previewPane, &PreviewPane::previewRequested, this, &TabPage::quickLookRequested);
    connect(m_previewPane, &PreviewPane::propertiesRequested, this, &TabPage::getInfoRequested);

    auto connectPreviewSelection = [this](QAbstractItemView* view) {
        if (view && view->selectionModel()) {
            connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                    [this](const QModelIndex&, const QModelIndex&) {
                        if (m_previewDebounceTimer)
                            m_previewDebounceTimer->start();
                    });
        }
    };
    connectPreviewSelection(m_iconView);
    connectPreviewSelection(m_listView);
    connectPreviewSelection(m_galleryView->filmstripView());

    m_searchEngine = new SearchEngine(this);
    connect(m_searchEngine, &SearchEngine::itemsFound, this, &TabPage::onSearchItemsFound);
    connect(m_searchEngine, &SearchEngine::searchFinished, this, &TabPage::onSearchFinished);
    connect(m_searchEngine, &SearchEngine::searchProgress, this, [this](const QString& dir) {
        if (!dir.isEmpty() && m_searchScopeBar->isVisible()) {
            QString display = QDir::toNativeSeparators(dir);
            if (display.size() > 70) {
                display = QStringLiteral("…") + display.right(69);
            }
            m_searchScopeBar->setStatusText(QStringLiteral("Searching %1").arg(display));
        }
    });

    m_searchBatchTimer = new QTimer(this);
    connect(m_searchBatchTimer, &QTimer::timeout, this, &TabPage::onSearchBatchTimer);

    // Typing in the search box should not tear down and restart a recursive
    // crawl on every keystroke.
    m_filterDebounceTimer = new QTimer(this);
    m_filterDebounceTimer->setSingleShot(true);
    m_filterDebounceTimer->setInterval(250);
    connect(m_filterDebounceTimer, &QTimer::timeout, this, &TabPage::runSearch);

    m_previewDebounceTimer = new QTimer(this);
    m_previewDebounceTimer->setSingleShot(true);
    m_previewDebounceTimer->setInterval(120);
    connect(m_previewDebounceTimer, &QTimer::timeout, this, &TabPage::updatePreviewPane);

    connect(m_searchScopeBar, &SearchScopeBar::scopeChanged, this,
            [this](SearchScope) { runSearch(); });
    connect(m_searchScopeBar, &SearchScopeBar::kindChanged, this,
            [this](const QString&) { runSearch(); });
    connect(m_searchScopeBar, &SearchScopeBar::subfoldersToggled, this,
            [this](bool) { runSearch(); });
    connect(m_searchScopeBar, &SearchScopeBar::contentToggled, this, [this](bool) { runSearch(); });
    connect(m_searchScopeBar, &SearchScopeBar::saveSearchRequested, this,
            &TabPage::saveSearchRequested);
    connect(m_searchScopeBar, &SearchScopeBar::clearRequested, this,
            &TabPage::searchClearRequested);

    // Forward view signals
    auto connectView = [this](auto* view) {
        connect(view, &std::remove_pointer_t<decltype(view)>::openPathRequested, this,
                [this](const QString& p) { navigateTo(p); });
        connect(view, &std::remove_pointer_t<decltype(view)>::quickLookRequested, this,
                &TabPage::quickLookRequested);
        connect(view, &std::remove_pointer_t<decltype(view)>::getInfoRequested, this,
                &TabPage::getInfoRequested);
        connect(view, &std::remove_pointer_t<decltype(view)>::contextMenuRequested, this,
                [this](const QPoint& pos, const QStringList& paths) {
                    emit contextMenuRequested(pos, paths, currentPath());
                });
    };

    connectView(m_iconView);
    connectView(m_listView);
    connectView(m_galleryView);

    connect(m_columnView, &ColumnView::openPathRequested, this,
            [this](const QString& p) { navigateTo(p); });
    connect(m_columnView, &ColumnView::quickLookRequested, this, &TabPage::quickLookRequested);
    connect(m_columnView, &ColumnView::getInfoRequested, this, &TabPage::getInfoRequested);
    connect(m_columnView, &ColumnView::contextMenuRequested, this,
            [this](const QPoint& pos, const QStringList& paths, const QString& colDir) {
                emit contextMenuRequested(pos, paths, !colDir.isEmpty() ? colDir : currentPath());
            });

    connect(m_columnView, &ColumnView::activeDirectoryChanged, this, [this](const QString& dir) {
        if (m_viewMode == ViewMode::Column && !dir.isEmpty() && m_currentPath != dir) {
            m_backHistory.append(m_currentPath);
            m_forwardHistory.clear();
            m_currentPath = dir;
            emit historyChanged();
            emit pathChanged(m_currentPath);
        }
        updateStatus();
    });

    connect(m_model, &FileSystemModel::directoryLoaded, this, &TabPage::onDirectoryLoaded);
    connect(m_model, &FileSystemModel::directoryLoading, this, [this](const QString& path) {
        if (path == m_model->rootPath()) {
            emit statusMessageChanged(QStringLiteral("Loading…"));
        }
    });
    connect(m_model, &FileSystemModel::fileDropRequested, this, &TabPage::fileDropRequested);
    connect(m_columnView, &ColumnView::fileDropRequested, this, &TabPage::fileDropRequested);
    connect(m_columnView, &ColumnView::contentLoaded, this, &TabPage::updateStatus);
    connect(m_model, &FileSystemModel::itemRenamed, this, &TabPage::itemRenamed);
    connect(m_columnView, &ColumnView::itemRenamed, this, &TabPage::itemRenamed);

    m_iconView->installEventFilter(this);
    m_listView->installEventFilter(this);
    m_columnView->installEventFilter(this);
    m_galleryView->installEventFilter(this);

    setViewMode(AppSettings::instance().viewMode());
    setPreviewPaneVisible(AppSettings::instance().showPreviewPane());
    navigateTo(initialPath, false);
    applyViewOptions();
}

bool TabPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        emit activated(this);
    }
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (!m_searchQuery.isEmpty() || m_model->isSearchResultsView()) {
                emit searchClearRequested();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TabPage::mousePressEvent(QMouseEvent* event) {
    emit activated(this);
    QWidget::mousePressEvent(event);
}

void TabPage::setActive(bool active) {
    m_isActive = active;
    setProperty("paneActive", active);

    if (property("splitPane").toBool()) {
        setStyleSheet(
            active
                ? QStringLiteral("TabPage#finderTabPage { border: 2px solid palette(highlight); }")
                : QStringLiteral("TabPage#finderTabPage { border: 2px solid transparent; }"));
    } else {
        setStyleSheet(QString());
    }

    style()->unpolish(this);
    style()->polish(this);
    update();
}

void TabPage::navigateTo(const QString& path, bool recordHistory) {
    if (path.startsWith(QStringLiteral("tag://"))) {
        showTaggedFiles(path.mid(6), recordHistory);
        return;
    }
    if (path.startsWith(QStringLiteral("smart://"))) {
        showSmartFolder(path.mid(8), recordHistory);
        return;
    }
    if (path.startsWith(QStringLiteral("recent://"))) {
        showRecentFiles(recordHistory);
        return;
    }

    QString cleanPath = QDir::cleanPath(path);
    if (!QDir(cleanPath).exists())
        return;

    if (m_model->isSearchResultsView() || !m_searchQuery.isEmpty()) {
        emit searchClearRequested();
    }

    if (recordHistory && !m_currentPath.isEmpty() && m_currentPath != cleanPath) {
        m_backHistory.append(m_currentPath);
        m_forwardHistory.clear();
        emit historyChanged();
    }

    // If returning from tag or search view to column view, restore previous column view
    if (m_currentPath.startsWith(QStringLiteral("tag://")) &&
        m_preSearchViewMode == ViewMode::Column && m_viewMode != ViewMode::Column) {
        setViewMode(ViewMode::Column);
    }

    m_currentPath = cleanPath;
    AppSettings::instance().addRecentFolder(cleanPath);
    if (m_previewPane)
        m_previewPane->clear();

    if (AppSettings::instance().rememberFolderViewModes()) {
        auto savedMode = AppSettings::instance().folderViewMode(m_currentPath);
        if (savedMode.has_value() && savedMode.value() != m_viewMode) {
            setViewMode(savedMode.value());
        }
    }

    if (m_viewMode == ViewMode::Column && m_columnView) {
        m_columnView->navigateToPath(m_currentPath);
    } else {
        m_model->setRootPath(m_currentPath);
    }

    applyViewOptions();
    emit pathChanged(m_currentPath);
}

void TabPage::showTaggedFiles(const QString& tag, bool recordHistory) {
    QString virtualPath = QStringLiteral("tag://") + tag;

    if (m_model->isSearchResultsView() || !m_searchQuery.isEmpty()) {
        emit searchClearRequested();
    }

    if (recordHistory && !m_currentPath.isEmpty() && m_currentPath != virtualPath) {
        m_backHistory.append(m_currentPath);
        m_forwardHistory.clear();
        emit historyChanged();
    }

    m_currentPath = virtualPath;

    // Tags cannot be displayed in Miller Column view since it's a flat virtual collection.
    // Temporarily switch to List View (or Icon View) and save previous view mode.
    if (m_viewMode == ViewMode::Column) {
        m_preSearchViewMode = m_viewMode;
        setViewMode(ViewMode::List);
    }

    m_model->setTaggedFiles(tag);
    updateStatus();

    emit pathChanged(m_currentPath);
}

void TabPage::goBack() {
    while (!m_backHistory.isEmpty()) {
        QString prev = m_backHistory.takeLast();
        if (prev.startsWith(QStringLiteral("tag://")) ||
            prev.startsWith(QStringLiteral("smart://")) ||
            prev.startsWith(QStringLiteral("recent://")) || QDir(prev).exists()) {
            m_forwardHistory.append(m_currentPath);
            emit historyChanged();
            navigateTo(prev, false);
            return;
        }
    }
    emit historyChanged();
}

void TabPage::goForward() {
    while (!m_forwardHistory.isEmpty()) {
        QString next = m_forwardHistory.takeLast();
        if (next.startsWith(QStringLiteral("tag://")) ||
            next.startsWith(QStringLiteral("smart://")) ||
            next.startsWith(QStringLiteral("recent://")) || QDir(next).exists()) {
            m_backHistory.append(m_currentPath);
            emit historyChanged();
            navigateTo(next, false);
            return;
        }
    }
    emit historyChanged();
}

void TabPage::goUp() {
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

QString TabPage::currentPath() const noexcept {
    if (m_currentPath.startsWith(QStringLiteral("tag://"))) {
        return m_currentPath;
    }
    if (m_viewMode == ViewMode::Column && m_columnView) {
        QString colDir = m_columnView->activeDirectory();
        if (!colDir.isEmpty())
            return colDir;
    }
    return m_currentPath;
}

void TabPage::refresh(const QString& pathToSelect) {
    QString target = !pathToSelect.isEmpty() ? pathToSelect : selectedFilePath();
    m_pendingSelections.clear();

    if (m_viewMode == ViewMode::Column) {
        m_pendingSelection.clear();
        m_columnView->refresh(target);
    } else {
        // The model may reload asynchronously; select once it has loaded.
        m_pendingSelection = target;
        m_model->refresh();
    }

    updateStatus();
}

void TabPage::refreshWithSelection(const QStringList& pathsToSelect) {
    if (pathsToSelect.isEmpty()) {
        refresh();
        return;
    }

    if (m_viewMode == ViewMode::Column) {
        m_pendingSelection.clear();
        m_pendingSelections.clear();
        m_columnView->refresh(pathsToSelect.first());
    } else {
        m_pendingSelection = pathsToSelect.first();
        m_pendingSelections = pathsToSelect;
        m_model->refresh();
    }

    updateStatus();
}

void TabPage::setViewMode(ViewMode mode) {
    m_viewMode = mode;
    m_viewStack->setCurrentIndex(static_cast<int>(mode));
    if (m_currentPath.startsWith(QStringLiteral("recent://"))) {
        applyViewOptions();
        applyPreviewPaneVisibility();
        updatePreviewPane();
        updateStatus();
        return;
    }
    if (AppSettings::instance().rememberFolderViewModes() && !m_currentPath.isEmpty() &&
        !m_currentPath.startsWith(QStringLiteral("tag://"))) {
        AppSettings::instance().setFolderViewMode(m_currentPath, mode);
    }
    if (mode == ViewMode::Column && m_columnView) {
        m_columnView->navigateToPath(m_currentPath);
        updateStatus();
    } else if (m_model) {
        m_model->setRootPath(m_currentPath);
    }
    applyViewOptions();
    applyPreviewPaneVisibility();
    updatePreviewPane();
}

void TabPage::setShowHidden(bool show) {
    m_model->setShowHidden(show);
    m_columnView->setShowHidden(show);
}

int TabPage::sortColumn() const {
    if (m_viewMode == ViewMode::Column && m_columnView) {
        return m_columnView->sortColumn();
    }
    return m_model->sortColumn();
}

Qt::SortOrder TabPage::sortOrder() const {
    if (m_viewMode == ViewMode::Column && m_columnView) {
        return m_columnView->sortOrder();
    }
    return m_model->sortOrder();
}

void TabPage::applySort(int column, Qt::SortOrder order) {
    m_model->sort(column, order);
    if (m_columnView) {
        m_columnView->applySort(column, order);
    }
}

void TabPage::setDirectoriesFirst(bool dirsFirst) {
    m_model->setDirectoriesFirst(dirsFirst);
}

void TabPage::applyIconSize(int size) {
    if (m_currentPath.isEmpty() || m_currentPath.startsWith(QStringLiteral("tag://"))) {
        if (m_iconView)
            m_iconView->setIconSizeValue(size);
        return;
    }
    FolderViewOptions options = AppSettings::instance().viewOptions(m_currentPath);
    options.iconSize = qBound(16, size, 256);
    AppSettings::instance().setViewOptions(m_currentPath, options);
    applyViewOptions();
}

void TabPage::applyViewOptions() {
    const FolderViewOptions options = AppSettings::instance().viewOptions(m_currentPath);
    if (m_iconView) {
        m_iconView->setFolderPath(m_currentPath);
        m_iconView->applyViewOptions(options);
    }
    if (m_listView)
        m_listView->applyViewOptions(options);
    if (m_columnView)
        m_columnView->setTextSize(options.textSize);
}

void TabPage::cleanUpIconView() {
    if (m_iconView)
        m_iconView->cleanUp();
}

void TabPage::setCalculateFolderSizes(bool enabled) {
    if (m_model) {
        m_model->setCalculateFolderSizes(enabled);
    }
}

void TabPage::setPreviewPaneVisible(bool visible) {
    m_previewPaneVisible = visible;
    applyPreviewPaneVisibility();
    updatePreviewPane();
}

void TabPage::applyPreviewPaneVisibility() {
    if (!m_previewPane)
        return;
    const bool visible = m_previewPaneVisible && m_viewMode != ViewMode::Column;
    m_previewPane->setVisible(visible);
    if (visible && m_viewSplitter) {
        const int total = m_viewSplitter->width();
        if (total > 320) {
            m_viewSplitter->setSizes({qMax(200, total - 280), 280});
        }
    }
}

void TabPage::updatePreviewPane() {
    if (!m_previewPane || !m_previewPaneVisible || m_viewMode == ViewMode::Column) {
        return;
    }
    const QString path = selectedFilePath();
    if (path.isEmpty()) {
        m_previewPane->clear();
    } else {
        m_previewPane->setFile(path);
    }
}

void TabPage::clearSmartFolderState() {
    m_activeSmartFolder = {};
    m_isSmartFolderView = false;
}

void TabPage::showSmartFolder(const SmartFolder& folder, bool recordHistory) {
    if (!folder.isValid())
        return;

    const QString virtualPath = QStringLiteral("smart://") + folder.name;
    if (recordHistory && !m_currentPath.isEmpty() && m_currentPath != virtualPath) {
        m_backHistory.append(m_currentPath);
        m_forwardHistory.clear();
        emit historyChanged();
    }

    // Smart folders are flat result lists; use List View like other searches.
    if (m_viewMode == ViewMode::Column) {
        m_preSearchViewMode = m_viewMode;
        setViewMode(ViewMode::List);
    } else {
        m_preSearchViewMode = m_viewMode;
    }

    m_searchEngine->stopSearch();
    if (m_searchBatchTimer->isActive())
        m_searchBatchTimer->stop();
    m_pendingSearchResults.clear();

    m_activeSmartFolder = folder;
    m_isSmartFolderView = true;
    m_currentPath = virtualPath;
    m_searchQuery = folder.filter.query;

    m_searchScopeBar->setFromFilter(folder.filter);
    m_searchScopeBar->setCurrentFolderName(folder.name);
    m_searchScopeBar->setSaveSearchEnabled(true);
    m_searchScopeBar->show();
    m_model->clearSearchResults();
    runSearch();
    emit pathChanged(m_currentPath);
    updateStatus();
}

void TabPage::showSmartFolder(const QString& name, bool recordHistory) {
    const SmartFolder folder = AppSettings::instance().smartFolder(name);
    if (folder.isValid()) {
        showSmartFolder(folder, recordHistory);
    }
}

void TabPage::showRecentFiles(bool recordHistory) {
    static const QString virtualPath = QStringLiteral("recent://Recents");
    clearSmartFolderState();
    if (recordHistory && !m_currentPath.isEmpty() && m_currentPath != virtualPath) {
        m_backHistory.append(m_currentPath);
        m_forwardHistory.clear();
        emit historyChanged();
    }

    if (m_viewMode == ViewMode::Column) {
        m_preSearchViewMode = m_viewMode;
        setViewMode(ViewMode::List);
    } else {
        m_preSearchViewMode = m_viewMode;
    }

    m_searchEngine->stopSearch();
    if (m_searchBatchTimer->isActive())
        m_searchBatchTimer->stop();
    m_pendingSearchResults.clear();
    m_searchQuery.clear();
    m_searchScopeBar->hide();
    m_currentPath = virtualPath;

    QVector<FileItem> items;
    for (const RecentFile& recent : RecentFilesManager::list(200)) {
        if (QFileInfo::exists(recent.path) || QFileInfo(recent.path).isSymLink()) {
            items.append(FileItem(recent.path));
        }
    }
    m_model->setSearchResults(items, false);
    if (m_previewPane)
        m_previewPane->clear();
    emit pathChanged(m_currentPath);
    updateStatus();
}

void TabPage::setFilterQuery(const QString& query) {
    m_searchQuery = query.trimmed();

    if (m_searchQuery.isEmpty()) {
        if (m_filterDebounceTimer)
            m_filterDebounceTimer->stop();
        m_searchEngine->stopSearch();
        if (m_searchBatchTimer->isActive())
            m_searchBatchTimer->stop();
        m_pendingSearchResults.clear();
        m_model->setFilterQuery(QString(), QString());

        if (m_isSmartFolderView) {
            // Clearing the field returns to the saved smart-folder results
            // instead of leaving the virtual folder.
            m_model->clearSearchResults();
            runSearch();
            return;
        }

        m_model->exitSearchResults();
        m_searchScopeBar->hide();
        if (m_viewMode != m_preSearchViewMode && m_preSearchViewMode == ViewMode::Column) {
            setViewMode(m_preSearchViewMode);
        }
        updateStatus();
        return;
    }

    m_searchScopeBar->setSaveSearchEnabled(true);
    const SearchFilter parsed = SearchQuery::parse(m_searchQuery);
    if (parsed.searchContents && !m_searchScopeBar->isContentSearch()) {
        m_searchScopeBar->setContentSearch(true);
    }
    if (m_searchScopeBar->isHidden()) {
        m_preSearchViewMode = m_viewMode;
        m_searchScopeBar->reset();
        m_searchScopeBar->show();
    }

    if (!m_isSmartFolderView) {
        QFileInfo fi(m_currentPath);
        m_searchScopeBar->setCurrentFolderName(fi.fileName().isEmpty() ? m_currentPath
                                                                       : fi.fileName());
    }

    if (m_filterDebounceTimer) {
        m_filterDebounceTimer->start();
    } else {
        runSearch();
    }
}

SearchFilter TabPage::buildSearchFilter() const {
    const SearchFilter parsed = SearchQuery::parse(m_searchQuery);
    SearchFilter filter;

    if (m_isSmartFolderView) {
        filter = m_activeSmartFolder.filter;
        if (!m_searchQuery.isEmpty()) {
            filter.query = m_searchQuery;
            filter.nameTerms = parsed.nameTerms;
            filter.excludeTerms = parsed.excludeTerms;
            filter.contentTerms = parsed.contentTerms;
            filter.kindFilter = parsed.kindFilter;
            filter.tags = parsed.tags;
            filter.extensions = parsed.extensions;
            filter.minSize = parsed.minSize;
            filter.maxSize = parsed.maxSize;
            filter.modifiedFrom = parsed.modifiedFrom;
            filter.modifiedTo = parsed.modifiedTo;
        }
    } else {
        filter = parsed;
    }

    filter.query = m_searchQuery;
    filter.includeHidden = AppSettings::instance().showHiddenFiles();

    if (m_isSmartFolderView) {
        if (filter.thisPC) {
            filter.searchPaths = SearchEngine::localSearchRoots();
        }
    } else {
        const QString kind = SearchQuery::kindFromToken(m_searchScopeBar->currentKind());
        if (!kind.isEmpty()) {
            filter.kindFilter = kind;
        }
        if (m_searchScopeBar->currentScope() == SearchScope::ThisPC) {
            filter.thisPC = true;
            filter.recursive = true;
            filter.searchPaths = SearchEngine::localSearchRoots();
        } else {
            filter.thisPC = false;
            filter.searchPath = (m_currentPath.startsWith(QStringLiteral("recent://")) ||
                                 m_currentPath.startsWith(QStringLiteral("apps://")))
                                    ? QDir::homePath()
                                    : m_currentPath;
            filter.recursive = m_searchScopeBar->isSubfolders();
        }
    }

    filter.useIndex = m_searchScopeBar->isIndexEnabled();
    const bool uiContent = m_searchScopeBar->isContentSearch();
    filter.searchContents = uiContent || parsed.searchContents;
    if (m_isSmartFolderView) {
        filter.searchContents = uiContent;
        if (parsed.searchContents)
            filter.searchContents = true;
    }
    if (filter.searchContents && filter.contentTerms.isEmpty()) {
        filter.contentTerms = filter.nameTerms;
    }

    return filter;
}

SearchFilter TabPage::currentSearchFilter() const {
    return buildSearchFilter();
}

void TabPage::runSearch() {
    if (m_filterDebounceTimer && m_filterDebounceTimer->isActive()) {
        m_filterDebounceTimer->stop();
    }
    if (m_searchQuery.isEmpty() && !m_isSmartFolderView) {
        setFilterQuery(QString());
        return;
    }

    const SearchFilter filter = buildSearchFilter();
    m_searchScopeBar->setFilterSummary(SearchQuery::summary(filter));

    const bool structured =
        !filter.excludeTerms.isEmpty() || !filter.contentTerms.isEmpty() || filter.searchContents ||
        !filter.tags.isEmpty() || !filter.extensions.isEmpty() || filter.minSize >= 0 ||
        filter.maxSize >= 0 || filter.modifiedFrom.isValid() || filter.modifiedTo.isValid();

    if (!filter.thisPC && !filter.recursive && !structured && !m_isSmartFolderView) {
        // Instant in-memory filter for plain non-recursive folder searches.
        m_searchEngine->stopSearch();
        if (m_searchBatchTimer->isActive())
            m_searchBatchTimer->stop();
        m_pendingSearchResults.clear();
        m_model->exitSearchResults();
        m_model->setFilterQuery(filter.nameTerms.join(QLatin1Char(' ')), filter.kindFilter);
        const int count = m_model->rowCount();
        m_searchScopeBar->setStatusText(QStringLiteral("%1 item%2 found")
                                            .arg(count)
                                            .arg(count == 1 ? QString() : QStringLiteral("s")));
        return;
    }

    m_searchEngine->stopSearch();
    if (m_searchBatchTimer->isActive())
        m_searchBatchTimer->stop();
    m_pendingSearchResults.clear();

    if (m_viewMode == ViewMode::Column) {
        setViewMode(ViewMode::List);
    }

    m_model->clearSearchResults();
    if (m_isSmartFolderView) {
        m_searchScopeBar->setStatusText(
            QStringLiteral("Searching smart folder \"%1\"...").arg(m_activeSmartFolder.name));
    } else if (filter.thisPC) {
        m_searchScopeBar->setStatusText(QStringLiteral("Searching This PC..."));
    } else {
        m_searchScopeBar->setStatusText(
            QStringLiteral("Searching \"%1\"...").arg(m_searchScopeBar->currentFolderName()));
    }

    m_searchEngine->startSearch(filter);
    m_searchBatchTimer->start(60);
}

void TabPage::onSearchItemsFound(const QVector<FileItem>& items) {
    m_pendingSearchResults += items;
}

void TabPage::onSearchBatchTimer() {
    if (!m_pendingSearchResults.isEmpty()) {
        m_model->appendSearchResults(m_pendingSearchResults);
        m_pendingSearchResults.clear();
        const int count = m_model->rowCount();
        m_searchScopeBar->setStatusText(QStringLiteral("%1 items found...").arg(count));
    }
}

void TabPage::onSearchFinished(int total) {
    onSearchBatchTimer();
    m_searchBatchTimer->stop();
    m_searchScopeBar->setStatusText(QStringLiteral("%1 item%2 found")
                                        .arg(total)
                                        .arg(total == 1 ? QString() : QStringLiteral("s")));
}

void TabPage::updateThemeStyles(bool isDark) {
    if (m_searchScopeBar) {
        m_searchScopeBar->updateThemeStyles(isDark);
    }
}

void TabPage::onDirectoryLoaded(const QString& /*path*/, int itemCount) {
    if (!m_pendingSelection.isEmpty()) {
        const QString target = m_pendingSelection;
        const QStringList multiSelection = m_pendingSelections;
        m_pendingSelection.clear();
        m_pendingSelections.clear();

        if (QFileInfo::exists(target)) {
            QStringList wanted = multiSelection;
            if (wanted.isEmpty()) {
                wanted.append(target);
            }

            QModelIndex currentIndex;
            QItemSelection selection;
            for (int r = 0; r < m_model->rowCount(); ++r) {
                const QModelIndex idx = m_model->index(r, 0);
                const QString filePath = idx.data(FileSystemModel::FilePathRole).toString();
                if (filePath == target) {
                    currentIndex = idx;
                }
                if (wanted.contains(filePath)) {
                    selection.select(idx, idx);
                }
            }

            if (m_viewMode == ViewMode::Icon) {
                if (currentIndex.isValid()) {
                    m_iconView->setCurrentIndex(currentIndex);
                    m_iconView->scrollTo(currentIndex);
                }
                if (!selection.isEmpty()) {
                    m_iconView->selectionModel()->select(
                        selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }
            } else if (m_viewMode == ViewMode::List) {
                if (currentIndex.isValid()) {
                    m_listView->setCurrentIndex(currentIndex);
                    m_listView->scrollTo(currentIndex);
                }
                if (!selection.isEmpty()) {
                    m_listView->selectionModel()->select(
                        selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                }
            }
        }
    }

    updateStatus();
}

void TabPage::updateStatus() {
    const int count = (m_viewMode == ViewMode::Column && m_columnView)
                          ? m_columnView->activeColumnRowCount()
                          : m_model->rowCount();
    QString msg =
        QStringLiteral("%1 item%2").arg(count).arg(count == 1 ? QString() : QStringLiteral("s"));
    emit statusMessageChanged(msg);
}

QString TabPage::selectedFilePath() const {
    switch (m_viewMode) {
    case ViewMode::Icon: {
        QModelIndex idx = m_iconView->currentIndex();
        if (idx.isValid())
            return idx.data(FileSystemModel::FilePathRole).toString();
        break;
    }
    case ViewMode::List: {
        QModelIndex idx = m_listView->currentIndex();
        if (idx.isValid())
            return idx.data(FileSystemModel::FilePathRole).toString();
        break;
    }
    case ViewMode::Column:
        return m_columnView->selectedPath();
    case ViewMode::Gallery:
        return m_galleryView->selectedPath();
    }
    return {};
}

QStringList TabPage::selectedFilePaths() const {
    if (m_viewMode == ViewMode::Column) {
        return m_columnView->selectedPaths();
    }
    if (m_viewMode == ViewMode::Gallery) {
        return m_galleryView->selectedFilePaths();
    }

    QStringList paths;
    QModelIndexList indexes;
    if (m_viewMode == ViewMode::Icon) {
        indexes = m_iconView->selectionModel()->selectedIndexes();
    } else if (m_viewMode == ViewMode::List) {
        indexes = m_listView->selectionModel()->selectedIndexes();
    }

    for (const auto& idx : indexes) {
        if (idx.column() != 0)
            continue;
        QString p = idx.data(FileSystemModel::FilePathRole).toString();
        if (!p.isEmpty() && !paths.contains(p)) {
            paths.append(p);
        }
    }
    return paths;
}

QAbstractItemView* TabPage::currentView() const {
    switch (m_viewMode) {
    case ViewMode::Icon:
        return m_iconView;
    case ViewMode::List:
        return m_listView;
    case ViewMode::Column:
        return m_columnView ? m_columnView->activeView() : nullptr;
    case ViewMode::Gallery:
        return m_galleryView ? m_galleryView->filmstripView() : nullptr;
    }
    return nullptr;
}

void TabPage::selectByPattern(const QString& pattern) {
    const QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QAbstractItemView* view = currentView();
    if (!view || !view->model() || !view->selectionModel()) {
        return;
    }

    const QRegularExpression regex(QRegularExpression::wildcardToRegularExpression(trimmed),
                                   QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        return;
    }

    QItemSelection selection;
    QModelIndex first;
    QAbstractItemModel* model = view->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QString name = index.data(FileSystemModel::FileNameRole).toString();
        if (regex.match(name).hasMatch()) {
            selection.select(index, index);
            if (!first.isValid()) {
                first = index;
            }
        }
    }

    view->selectionModel()->select(selection,
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (first.isValid()) {
        view->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        view->scrollTo(first);
    }
}

void TabPage::invertSelection() {
    QAbstractItemView* view = currentView();
    if (!view || !view->model() || !view->selectionModel()) {
        return;
    }

    QAbstractItemModel* model = view->model();
    QItemSelectionModel* selectionModel = view->selectionModel();
    QItemSelection selection;
    QModelIndex first;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        if (!selectionModel->isSelected(index)) {
            selection.select(index, index);
            if (!first.isValid()) {
                first = index;
            }
        }
    }

    selectionModel->select(selection,
                           QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (first.isValid()) {
        view->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        view->scrollTo(first);
    }
}

void TabPage::selectAll() {
    if (m_viewMode == ViewMode::Icon) {
        m_iconView->selectAll();
    } else if (m_viewMode == ViewMode::List) {
        m_listView->selectAll();
    } else if (m_viewMode == ViewMode::Column) {
        m_columnView->selectAll();
    } else if (m_viewMode == ViewMode::Gallery) {
        m_galleryView->selectAll();
    }
}

bool TabPage::triggerRename() {
    switch (m_viewMode) {
    case ViewMode::Icon: {
        QModelIndex idx = m_iconView->currentIndex();
        if (idx.isValid()) {
            m_iconView->edit(idx);
            return true;
        }
        break;
    }
    case ViewMode::List: {
        QModelIndex idx = m_listView->currentIndex();
        if (idx.isValid()) {
            if (idx.column() != FileSystemModel::ColName && m_model) {
                idx = m_model->index(idx.row(), FileSystemModel::ColName);
            }
            m_listView->edit(idx);
            return true;
        }
        break;
    }
    case ViewMode::Column: {
        if (m_columnView) {
            return m_columnView->triggerRename();
        }
        break;
    }
    case ViewMode::Gallery:
        break;
    }
    return false;
}
