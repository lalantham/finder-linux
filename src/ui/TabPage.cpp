#include "TabPage.h"
#include <QVBoxLayout>
#include <QFileInfo>
#include <QDir>

TabPage::TabPage(const QString& initialPath, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_model = new FileSystemModel(this);
    m_model->setShowHidden(AppSettings::instance().showHiddenFiles());
    m_model->setDirectoriesFirst(AppSettings::instance().directoriesFirst());

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
    connect(m_searchEngine, &SearchEngine::itemFound, this, &TabPage::onSearchItemFound);
    connect(m_searchEngine, &SearchEngine::searchFinished, this, &TabPage::onSearchFinished);

    m_searchBatchTimer = new QTimer(this);
    connect(m_searchBatchTimer, &QTimer::timeout, this, &TabPage::onSearchBatchTimer);

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
    });

    connect(m_model, &FileSystemModel::directoryLoaded, this, &TabPage::onDirectoryLoaded);
    connect(m_model, &FileSystemModel::fileDropRequested, this, &TabPage::fileDropRequested);
    connect(m_columnView, &ColumnView::fileDropRequested, this, &TabPage::fileDropRequested);

    setViewMode(AppSettings::instance().viewMode());
    navigateTo(initialPath, false);
}

void TabPage::navigateTo(const QString& path, bool recordHistory)
{
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

    m_currentPath = cleanPath;
    m_model->setRootPath(m_currentPath);
    if (m_viewMode == ViewMode::Column && m_columnView) {
        m_columnView->navigateToPath(m_currentPath);
    } else {
        m_columnView->setRootPath(m_currentPath);
    }

    emit pathChanged(m_currentPath);
}

void TabPage::goBack()
{
    while (!m_backHistory.isEmpty()) {
        QString prev = m_backHistory.takeLast();
        if (QDir(prev).exists()) {
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
        if (QDir(next).exists()) {
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
    if (m_viewMode == ViewMode::Column && m_columnView) {
        QString colDir = m_columnView->activeDirectory();
        if (!colDir.isEmpty()) return colDir;
    }
    return m_currentPath;
}

void TabPage::refresh(const QString& pathToSelect)
{
    QString target = !pathToSelect.isEmpty() ? pathToSelect : selectedFilePath();

    m_model->refresh();
    m_columnView->refresh(target);

    if (!target.isEmpty() && QFileInfo::exists(target)) {
        for (int r = 0; r < m_model->rowCount(); ++r) {
            QModelIndex idx = m_model->index(r, 0);
            if (idx.data(FileSystemModel::FilePathRole).toString() == target) {
                if (m_viewMode == ViewMode::Icon) {
                    m_iconView->setCurrentIndex(idx);
                    m_iconView->scrollTo(idx);
                } else if (m_viewMode == ViewMode::List) {
                    m_listView->setCurrentIndex(idx);
                    m_listView->scrollTo(idx);
                }
                break;
            }
        }
    }
}

void TabPage::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
    m_viewStack->setCurrentIndex(static_cast<int>(mode));
    if (mode == ViewMode::Column && m_columnView) {
        m_columnView->navigateToPath(m_currentPath);
    } else if (m_model) {
        m_model->setRootPath(m_currentPath);
    }
}

void TabPage::setShowHidden(bool show)
{
    m_model->setShowHidden(show);
    m_columnView->setShowHidden(show);
}

void TabPage::setFilterQuery(const QString& query)
{
    m_searchQuery = query.trimmed();
    if (m_searchQuery.isEmpty()) {
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
        runSearch();
    }
}

void TabPage::runSearch()
{
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

void TabPage::onSearchItemFound(const FileItem& item)
{
    m_pendingSearchResults.append(item);
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
    updateStatus();
}

void TabPage::updateStatus()
{
    int count = m_model->rowCount();
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
