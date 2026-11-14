#include "TabPage.h"
#include <QVBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QItemSelection>
#include <QRegularExpression>
#include <QAbstractItemView>
#include <QMouseEvent>
#include <QStyle>

TabPage::TabPage(const QString& initialPath, QWidget* parent)
    : QWidget(parent)
{
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
    m_searchScopeBar->hide();
    layout->addWidget(m_searchScopeBar);

    layout->addWidget(m_viewStack);

    m_searchEngine = new SearchEngine(this);
    connect(m_searchEngine, &SearchEngine::itemsFound, this, &TabPage::onSearchItemsFound);
    connect(m_searchEngine, &SearchEngine::searchFinished, this, &TabPage::onSearchFinished);

    m_searchBatchTimer = new QTimer(this);
    connect(m_searchBatchTimer, &QTimer::timeout, this, &TabPage::onSearchBatchTimer);

    // Typing in the search box should not tear down and restart a recursive
    // crawl on every keystroke.
    m_filterDebounceTimer = new QTimer(this);
    m_filterDebounceTimer->setSingleShot(true);
    m_filterDebounceTimer->setInterval(250);
    connect(m_filterDebounceTimer, &QTimer::timeout, this, &TabPage::runSearch);

    connect(m_searchScopeBar, &SearchScopeBar::scopeChanged, this, [this](SearchScope) {
        runSearch();
    });
    connect(m_searchScopeBar, &SearchScopeBar::kindChanged, this, [this](const QString&) {
        runSearch();
    });
    connect(m_searchScopeBar, &SearchScopeBar::subfoldersToggled, this, [this](bool) {
        runSearch();
    });
    connect(m_searchScopeBar, &SearchScopeBar::clearRequested, this, &TabPage::searchClearRequested);

    // Forward view signals
    auto connectView = [this](auto* view) {
        connect(view, &std::remove_pointer_t<decltype(view)>::openPathRequested, this, [this](const QString& p) {
            navigateTo(p);
        });
        connect(view, &std::remove_pointer_t<decltype(view)>::quickLookRequested, this, &TabPage::quickLookRequested);
        connect(view, &std::remove_pointer_t<decltype(view)>::getInfoRequested, this, &TabPage::getInfoRequested);
        connect(view, &std::remove_pointer_t<decltype(view)>::contextMenuRequested, this, [this](const QPoint& pos, const QStringList& paths) {
            emit contextMenuRequested(pos, paths, currentPath());
        });
    };

    connectView(m_iconView);
    connectView(m_listView);
    connectView(m_galleryView);

    connect(m_columnView, &ColumnView::openPathRequested, this, [this](const QString& p) {
        navigateTo(p);
    });
    connect(m_columnView, &ColumnView::quickLookRequested, this, &TabPage::quickLookRequested);
    connect(m_columnView, &ColumnView::getInfoRequested, this, &TabPage::getInfoRequested);
    connect(m_columnView, &ColumnView::contextMenuRequested, this, [this](const QPoint& pos, const QStringList& paths, const QString& colDir) {
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
    navigateTo(initialPath, false);
}

bool TabPage::eventFilter(QObject* watched, QEvent* event)
{
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

void TabPage::mousePressEvent(QMouseEvent* event)
{
    emit activated(this);
    QWidget::mousePressEvent(event);
}

void TabPage::setActive(bool active)
{
    m_isActive = active;
    setProperty("paneActive", active);

    if (property("splitPane").toBool()) {
        setStyleSheet(active
            ? QStringLiteral("TabPage#finderTabPage { border: 2px solid palette(highlight); }")
            : QStringLiteral("TabPage#finderTabPage { border: 2px solid transparent; }"));
    } else {
        setStyleSheet(QString());
    }

    style()->unpolish(this);
    style()->polish(this);
    update();
}

void TabPage::navigateTo(const QString& path, bool recordHistory)
{
    if (path.startsWith(QStringLiteral("tag://"))) {
        showTaggedFiles(path.mid(6), recordHistory);
        return;
    }

    QString cleanPath = QDir::cleanPath(path);
    if (!QDir(cleanPath).exists()) return;

    if (m_model->isSearchResultsView() || !m_searchQuery.isEmpty()) {
        emit searchClearRequested();
    }

    if (recordHistory && !m_currentPath.isEmpty() && m_currentPath != cleanPath) {
        m_backHistory.append(m_currentPath);
        m_forwardHistory.clear();
        emit historyChanged();
    }

    // If returning from tag or search view to column view, restore previous column view
    if (m_currentPath.startsWith(QStringLiteral("tag://")) && m_preSearchViewMode == ViewMode::Column && m_viewMode != ViewMode::Column) {
        setViewMode(ViewMode::Column);
    }

    m_currentPath = cleanPath;

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

    emit pathChanged(m_currentPath);
}

void TabPage::showTaggedFiles(const QString& tag, bool recordHistory)
{
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

void TabPage::goBack()
{
    while (!m_backHistory.isEmpty()) {
        QString prev = m_backHistory.takeLast();
        if (prev.startsWith(QStringLiteral("tag://")) || QDir(prev).exists()) {
            m_forwardHistory.append(m_currentPath);
            emit historyChanged();
            navigateTo(prev, false);
            return;
        }
    }
    emit historyChanged();
}

void TabPage::goForward()
{
    while (!m_forwardHistory.isEmpty()) {
        QString next = m_forwardHistory.takeLast();
        if (next.startsWith(QStringLiteral("tag://")) || QDir(next).exists()) {
            m_backHistory.append(m_currentPath);
            emit historyChanged();
            navigateTo(next, false);
            return;
        }
    }
    emit historyChanged();
}

void TabPage::goUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

QString TabPage::currentPath() const noexcept
{
    if (m_currentPath.startsWith(QStringLiteral("tag://"))) {
        return m_currentPath;
    }
    if (m_viewMode == ViewMode::Column && m_columnView) {
        QString colDir = m_columnView->activeDirectory();
        if (!colDir.isEmpty()) return colDir;
    }
    return m_currentPath;
}

void TabPage::refresh(const QString& pathToSelect)
{
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

void TabPage::refreshWithSelection(const QStringList& pathsToSelect)
{
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

void TabPage::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
    m_viewStack->setCurrentIndex(static_cast<int>(mode));
    if (AppSettings::instance().rememberFolderViewModes() && !m_currentPath.isEmpty() && !m_currentPath.startsWith(QStringLiteral("tag://"))) {
        AppSettings::instance().setFolderViewMode(m_currentPath, mode);
    }
    if (mode == ViewMode::Column && m_columnView) {
        m_columnView->navigateToPath(m_currentPath);
        updateStatus();
    } else if (m_model) {
        m_model->setRootPath(m_currentPath);
    }
}

void TabPage::setShowHidden(bool show)
{
    m_model->setShowHidden(show);
    m_columnView->setShowHidden(show);
}

int TabPage::sortColumn() const
{
    if (m_viewMode == ViewMode::Column && m_columnView) {
        return m_columnView->sortColumn();
    }
    return m_model->sortColumn();
}

Qt::SortOrder TabPage::sortOrder() const
{
    if (m_viewMode == ViewMode::Column && m_columnView) {
        return m_columnView->sortOrder();
    }
    return m_model->sortOrder();
}

void TabPage::applySort(int column, Qt::SortOrder order)
{
    m_model->sort(column, order);
    if (m_columnView) {
        m_columnView->applySort(column, order);
    }
}

void TabPage::setDirectoriesFirst(bool dirsFirst)
{
    m_model->setDirectoriesFirst(dirsFirst);
}

void TabPage::applyIconSize(int size)
{
    if (m_iconView) {
        m_iconView->setIconSizeValue(size);
    }
}

void TabPage::setCalculateFolderSizes(bool enabled)
{
    if (m_model) {
        m_model->setCalculateFolderSizes(enabled);
    }
}

void TabPage::setFilterQuery(const QString& query)
{
    m_searchQuery = query.trimmed();
    if (m_searchQuery.isEmpty()) {
        if (m_filterDebounceTimer) m_filterDebounceTimer->stop();
        m_searchScopeBar->hide();
        m_searchEngine->stopSearch();
        if (m_searchBatchTimer->isActive()) {
            m_searchBatchTimer->stop();
        }
        m_pendingSearchResults.clear();
        m_model->exitSearchResults();
        m_model->setFilterQuery(QString(), QString());
        if (m_viewMode != m_preSearchViewMode && m_preSearchViewMode == ViewMode::Column) {
            setViewMode(m_preSearchViewMode);
        }
        updateStatus();
    } else {
        if (m_searchScopeBar->isHidden()) {
            m_preSearchViewMode = m_viewMode;
            m_searchScopeBar->reset();
            m_searchScopeBar->show();
        }
        QFileInfo fi(m_currentPath);
        m_searchScopeBar->setCurrentFolderName(fi.fileName().isEmpty() ? m_currentPath : fi.fileName());
        if (m_filterDebounceTimer) {
            m_filterDebounceTimer->start();
        } else {
            runSearch();
        }
    }
}

void TabPage::runSearch()
{
    if (m_filterDebounceTimer && m_filterDebounceTimer->isActive()) {
        m_filterDebounceTimer->stop();
    }
    if (m_searchQuery.isEmpty()) {
        setFilterQuery(QString());
        return;
    }

    SearchScope scope = m_searchScopeBar->currentScope();
    QString kind = m_searchScopeBar->currentKind();
    bool subfolders = m_searchScopeBar->isSubfolders();

    if (scope == SearchScope::ThisFolder && !subfolders) {
        // Mode 1: Instant In-Memory Filter on Current Folder
        m_searchEngine->stopSearch();
        if (m_searchBatchTimer->isActive()) m_searchBatchTimer->stop();
        m_pendingSearchResults.clear();
        m_model->exitSearchResults();
        m_model->setFilterQuery(m_searchQuery, kind);
        int count = m_model->rowCount();
        m_searchScopeBar->setStatusText(QStringLiteral("%1 item%2 found").arg(count).arg(count == 1 ? "" : "s"));
    } else {
        // Mode 2: Recursive Search (This Folder Subdirectories OR Everywhere)
        m_searchEngine->stopSearch();
        if (m_searchBatchTimer->isActive()) m_searchBatchTimer->stop();
        m_pendingSearchResults.clear();

        QString searchPath = (scope == SearchScope::ThisFolder) ? m_currentPath : QDir::homePath();

        // If in Column view, switch to List view for flat search results
        if (m_viewMode == ViewMode::Column) {
            setViewMode(ViewMode::List);
        }

        m_model->clearSearchResults();
        m_searchScopeBar->setStatusText(scope == SearchScope::ThisFolder
            ? QStringLiteral("Searching \"%1\"...").arg(m_searchScopeBar->currentFolderName())
            : QStringLiteral("Searching Everywhere..."));

        SearchFilter filter;
        filter.query = m_searchQuery;
        filter.searchPath = searchPath;
        filter.recursive = true;
        filter.kindFilter = kind;
        filter.includeHidden = AppSettings::instance().showHiddenFiles();

        m_searchEngine->startSearch(filter);
        m_searchBatchTimer->start(60);
    }
}

void TabPage::onSearchItemsFound(const QVector<FileItem>& items)
{
    m_pendingSearchResults += items;
}

void TabPage::onSearchBatchTimer()
{
    if (!m_pendingSearchResults.isEmpty()) {
        m_model->appendSearchResults(m_pendingSearchResults);
        m_pendingSearchResults.clear();
        int count = m_model->rowCount();
        m_searchScopeBar->setStatusText(QStringLiteral("%1 items found...").arg(count));
    }
}

void TabPage::onSearchFinished(int total)
{
    onSearchBatchTimer();
    m_searchBatchTimer->stop();
    m_searchScopeBar->setStatusText(QStringLiteral("%1 item%2 found").arg(total).arg(total == 1 ? "" : "s"));
}

void TabPage::updateThemeStyles(bool isDark)
{
    if (m_searchScopeBar) {
        m_searchScopeBar->updateThemeStyles(isDark);
    }
}

void TabPage::onDirectoryLoaded(const QString& /*path*/, int itemCount)
{
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

void TabPage::updateStatus()
{
    const int count = (m_viewMode == ViewMode::Column && m_columnView)
        ? m_columnView->activeColumnRowCount()
        : m_model->rowCount();
    QString msg = QStringLiteral("%1 item%2").arg(count).arg(count == 1 ? QString() : QStringLiteral("s"));
    emit statusMessageChanged(msg);
}

QString TabPage::selectedFilePath() const
{
    switch (m_viewMode) {
    case ViewMode::Icon: {
        QModelIndex idx = m_iconView->currentIndex();
        if (idx.isValid()) return idx.data(FileSystemModel::FilePathRole).toString();
        break;
    }
    case ViewMode::List: {
        QModelIndex idx = m_listView->currentIndex();
        if (idx.isValid()) return idx.data(FileSystemModel::FilePathRole).toString();
        break;
    }
    case ViewMode::Column:
        return m_columnView->selectedPath();
    case ViewMode::Gallery:
        return m_galleryView->selectedPath();
    }
    return {};
}

QStringList TabPage::selectedFilePaths() const
{
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
        if (idx.column() != 0) continue;
        QString p = idx.data(FileSystemModel::FilePathRole).toString();
        if (!p.isEmpty() && !paths.contains(p)) {
            paths.append(p);
        }
    }
    return paths;
}

QAbstractItemView* TabPage::currentView() const
{
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

void TabPage::selectByPattern(const QString& pattern)
{
    const QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QAbstractItemView* view = currentView();
    if (!view || !view->model() || !view->selectionModel()) {
        return;
    }

    const QRegularExpression regex(
        QRegularExpression::wildcardToRegularExpression(trimmed),
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

void TabPage::invertSelection()
{
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

void TabPage::selectAll()
{
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

bool TabPage::triggerRename()
{
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
