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

    layout->addWidget(m_viewStack);

    // Forward view signals
    auto connectView = [this](auto* view) {
        connect(view, &std::remove_pointer_t<decltype(view)>::openPathRequested, this, [this](const QString& p) {
            navigateTo(p);
        });
        connect(view, &std::remove_pointer_t<decltype(view)>::quickLookRequested, this, &TabPage::quickLookRequested);
        connect(view, &std::remove_pointer_t<decltype(view)>::getInfoRequested, this, &TabPage::getInfoRequested);
        connect(view, &std::remove_pointer_t<decltype(view)>::contextMenuRequested, this, [this](const QPoint& pos, const QStringList& paths) {
            emit contextMenuRequested(pos, paths, m_currentPath);
        });
    };

    connectView(m_iconView);
    connectView(m_listView);
    connectView(m_columnView);
    connectView(m_galleryView);

    connect(m_model, &FileSystemModel::directoryLoaded, this, &TabPage::onDirectoryLoaded);

    setViewMode(AppSettings::instance().viewMode());
    navigateTo(initialPath, false);
}

void TabPage::navigateTo(const QString& path, bool recordHistory)
{
    QString cleanPath = QDir::cleanPath(path);
    if (!QDir(cleanPath).exists()) return;

    if (recordHistory && !m_currentPath.isEmpty() && m_currentPath != cleanPath) {
        m_backHistory.append(m_currentPath);
        m_forwardHistory.clear();
        emit historyChanged();
    }

    m_currentPath = cleanPath;
    m_model->setRootPath(m_currentPath);
    m_columnView->setRootPath(m_currentPath);

    emit pathChanged(m_currentPath);
}

void TabPage::goBack()
{
    if (m_backHistory.isEmpty()) return;
    QString prev = m_backHistory.takeLast();
    m_forwardHistory.append(m_currentPath);
    emit historyChanged();

    navigateTo(prev, false);
}

void TabPage::goForward()
{
    if (m_forwardHistory.isEmpty()) return;
    QString next = m_forwardHistory.takeLast();
    m_backHistory.append(m_currentPath);
    emit historyChanged();

    navigateTo(next, false);
}

void TabPage::goUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

void TabPage::refresh()
{
    m_model->refresh();
    m_columnView->refresh();
}

void TabPage::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
    m_viewStack->setCurrentIndex(static_cast<int>(mode));
}

void TabPage::setShowHidden(bool show)
{
    m_model->setShowHidden(show);
}

void TabPage::setFilterQuery(const QString& query)
{
    // Filter items in the current directory if query is present
    if (query.isEmpty()) {
        m_model->refresh();
    } else {
        // Can filter or re-populate matching items
        m_model->refresh();
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
        indexes = m_listView->selectionModel()->selectedRows();
    }

    for (const auto& idx : indexes) {
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
    }
}
