#include "ListView.h"
#include "AppSettings.h"
#include "DesktopAppsManager.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>

ListView::ListView(QWidget* parent) : QTreeView(parent) {
    setRootIsDecorated(false);
    setAlternatingRowColors(true);
    setSortingEnabled(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAllColumnsShowFocus(true);
    setUniformRowHeights(true);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);
    header()->setSectionsMovable(true);
    header()->setStretchLastSection(true);
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_delegate = new FileItemDelegate(this);
    setItemDelegate(m_delegate);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &ListView::onSpringTimeout);

    connect(this, &QTreeView::activated, this, &ListView::onItemActivated);

    m_layoutSaveTimer = new QTimer(this);
    m_layoutSaveTimer->setSingleShot(true);
    m_layoutSaveTimer->setInterval(300);
    connect(m_layoutSaveTimer, &QTimer::timeout, this, &ListView::saveColumnSettings);

    connect(header(), &QHeaderView::sectionResized, this, [this](int, int, int) {
        if (!m_applyingColumns && m_layoutSaveTimer)
            m_layoutSaveTimer->start();
    });
    connect(header(), &QHeaderView::sectionMoved, this, [this](int, int, int) {
        if (!m_applyingColumns && m_layoutSaveTimer)
            m_layoutSaveTimer->start();
    });
    connect(header(), &QHeaderView::customContextMenuRequested, this, &ListView::showHeaderMenu);
}

void ListView::setFileSystemModel(FileSystemModel* model) {
    if (m_fsModel != model) {
        if (m_fsModel) {
            disconnect(m_fsModel, &FileSystemModel::sortChanged, this,
                       &ListView::onModelSortChanged);
        }
        m_fsModel = model;
        if (model) {
            connect(model, &FileSystemModel::sortChanged, this, &ListView::onModelSortChanged);
        }
    }

    // QTreeView::setModel() sorts through the header's sort indicator, which
    // still defaults to column 0 ascending. Seed it from the model first,
    // otherwise attaching the model silently discards the sort order the
    // caller restored from settings (and then persists the wrong one).
    if (model) {
        syncSortIndicator(model->sortColumn(), model->sortOrder());
    }
    setModel(model);
    applyColumnConfiguration();
}

void ListView::syncSortIndicator(int column, Qt::SortOrder order) {
    if (column < 0 || column >= FileSystemModel::ColumnCount) {
        return;
    }
    if (header()->sortIndicatorSection() == column && header()->sortIndicatorOrder() == order &&
        header()->isSortIndicatorShown()) {
        return;
    }

    // Updating the indicator drives QTreeView's own sortByColumn(); the model
    // already holds this order, so suppress the feedback loop.
    m_syncingSort = true;
    const QSignalBlocker blocker(header());
    header()->setSortIndicatorShown(true);
    header()->setSortIndicator(column, order);
    m_syncingSort = false;
}

void ListView::onModelSortChanged(int column, Qt::SortOrder order) {
    if (m_syncingSort) {
        return;
    }
    syncSortIndicator(column, order);
}

void ListView::setTextSize(int size) {
    QFont f = font();
    f.setPointSize(qBound(8, size, 24));
    setFont(f);
    m_delegate->setTextSize(f.pointSize());
    viewport()->update();
}

void ListView::applyViewOptions(const FolderViewOptions& options) {
    setTextSize(options.textSize);
    applyColumnConfiguration();
}

int ListView::defaultColumnWidth(int column) const {
    switch (column) {
    case FileSystemModel::ColName:
        return 260;
    case FileSystemModel::ColDateModified:
        return 150;
    case FileSystemModel::ColSize:
        return 90;
    case FileSystemModel::ColKind:
        return 140;
    case FileSystemModel::ColDateCreated:
        return 150;
    case FileSystemModel::ColDateOpened:
        return 150;
    case FileSystemModel::ColTags:
        return 120;
    case FileSystemModel::ColPermissions:
        return 100;
    case FileSystemModel::ColOwner:
        return 90;
    case FileSystemModel::ColGroup:
        return 90;
    case FileSystemModel::ColComment:
        return 180;
    default:
        return 120;
    }
}

void ListView::applyColumnConfiguration() {
    if (!m_fsModel)
        return;

    m_applyingColumns = true;
    const QSignalBlocker blocker(header());

    const QStringList visible = AppSettings::instance().listVisibleColumns();
    for (int column = 0; column < FileSystemModel::ColumnCount; ++column) {
        const bool show = column == FileSystemModel::ColName ||
                          visible.contains(FileSystemModel::columnKey(column));
        setColumnHidden(column, !show);
    }

    QStringList order = AppSettings::instance().listColumnOrder();
    if (order.isEmpty())
        order = AppSettings::instance().defaultListColumns();
    for (int visual = 0; visual < order.size(); ++visual) {
        const int logical = FileSystemModel::columnForKey(order.at(visual));
        if (logical < 0)
            continue;
        const int currentVisual = header()->visualIndex(logical);
        if (currentVisual >= 0 && currentVisual != visual) {
            header()->moveSection(currentVisual, visual);
        }
    }

    const QVariantMap widths = AppSettings::instance().listColumnWidths();
    for (int column = 0; column < FileSystemModel::ColumnCount; ++column) {
        const QString key = FileSystemModel::columnKey(column);
        bool ok = false;
        const int savedWidth = widths.value(key).toInt(&ok);
        setColumnWidth(column, ok && savedWidth > 0 ? savedWidth : defaultColumnWidth(column));
    }

    m_applyingColumns = false;
}

void ListView::saveColumnSettings() {
    if (!m_fsModel || m_applyingColumns)
        return;

    QVariantMap widths;
    for (int column = 0; column < FileSystemModel::ColumnCount; ++column) {
        widths.insert(FileSystemModel::columnKey(column), columnWidth(column));
    }
    AppSettings::instance().setListColumnWidths(widths);

    QList<int> logicalColumns = FileSystemModel::allColumns();
    std::sort(logicalColumns.begin(), logicalColumns.end(),
              [this](int a, int b) { return header()->visualIndex(a) < header()->visualIndex(b); });
    QStringList order;
    order.reserve(logicalColumns.size());
    for (const int column : logicalColumns) {
        order.append(FileSystemModel::columnKey(column));
    }
    AppSettings::instance().setListColumnOrder(order);
}

void ListView::setColumnVisible(int column, bool visible) {
    if (!m_fsModel || column < 0 || column >= FileSystemModel::ColumnCount)
        return;
    if (column == FileSystemModel::ColName)
        visible = true;

    QStringList columns = AppSettings::instance().listVisibleColumns();
    const QString key = FileSystemModel::columnKey(column);
    if (visible && !columns.contains(key)) {
        columns.append(key);
    } else if (!visible) {
        columns.removeAll(key);
    }
    AppSettings::instance().setListVisibleColumns(columns);
    applyColumnConfiguration();
}

void ListView::showHeaderMenu(const QPoint& pos) {
    QMenu menu(this);
    for (const int column : FileSystemModel::allColumns()) {
        QAction* action = menu.addAction(FileSystemModel::columnTitle(column));
        action->setCheckable(true);
        action->setChecked(column == FileSystemModel::ColName || !isColumnHidden(column));
        action->setEnabled(column != FileSystemModel::ColName);
        connect(action, &QAction::toggled, this,
                [this, column](bool checked) { setColumnVisible(column, checked); });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("Show All Columns"), this, [this]() {
        QStringList all;
        for (const int column : FileSystemModel::allColumns()) {
            all.append(FileSystemModel::columnKey(column));
        }
        AppSettings::instance().setListVisibleColumns(all);
        applyColumnConfiguration();
    });
    menu.addAction(QStringLiteral("Reset Columns"), this, [this]() {
        AppSettings::instance().setListVisibleColumns(AppSettings::instance().defaultListColumns());
        AppSettings::instance().setListColumnOrder(AppSettings::instance().defaultListColumns());
        AppSettings::instance().setListColumnWidths({});
        applyColumnConfiguration();
    });
    menu.exec(header()->mapToGlobal(pos));
}

void ListView::onItemActivated(const QModelIndex& index) {
    if (!index.isValid())
        return;
    QString path = index.data(FileSystemModel::FilePathRole).toString();
    bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();

    if (isDir) {
        emit openPathRequested(path);
    } else {
        DesktopAppsManager::launchFile(path);
    }
}

void ListView::mouseDoubleClickEvent(QMouseEvent* event) {
    QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        onItemActivated(idx);
    } else {
        QTreeView::mouseDoubleClickEvent(event);
    }
}

void ListView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            emit quickLookRequested(idx.data(FileSystemModel::FilePathRole).toString());
        }
        return;
    }
    if (event->key() == Qt::Key_F2) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            if (idx.column() != FileSystemModel::ColName && model()) {
                idx = model()->index(idx.row(), FileSystemModel::ColName);
            }
            edit(idx);
        }
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            onItemActivated(idx);
        }
        return;
    }
    QTreeView::keyPressEvent(event);
}

void ListView::contextMenuEvent(QContextMenuEvent* event) {
    QModelIndex idx = indexAt(event->pos());
    QStringList paths;
    if (idx.isValid()) {
        if (!selectionModel()->isSelected(idx)) {
            setCurrentIndex(idx);
        }
        QModelIndexList selected = selectionModel()->selectedRows();
        for (const auto& i : selected) {
            QString p = i.data(FileSystemModel::FilePathRole).toString();
            if (!p.isEmpty() && !paths.contains(p))
                paths.append(p);
        }
        if (paths.isEmpty()) {
            paths.append(idx.data(FileSystemModel::FilePathRole).toString());
        }
    }
    emit contextMenuRequested(event->globalPos(), paths);
}

void ListView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QTreeView::dragEnterEvent(event);
    }
}

void ListView::dragMoveEvent(QDragMoveEvent* event) {
    if (!AppSettings::instance().springLoadedFoldersEnabled() || !event->mimeData()->hasUrls()) {
        resetSpringTimer();
        QTreeView::dragMoveEvent(event);
        return;
    }

    QModelIndex idx = indexAt(event->position().toPoint());
    if (idx.isValid()) {
        QModelIndex nameIdx = idx.sibling(idx.row(), FileSystemModel::ColName);
        if (nameIdx.data(FileSystemModel::FileIsDirRole).toBool()) {
            QString targetPath = nameIdx.data(FileSystemModel::FilePathRole).toString();

            bool safe = true;
            for (const auto& url : event->mimeData()->urls()) {
                if (url.isLocalFile() &&
                    FileSystemModel::isSameOrInsidePath(targetPath, url.toLocalFile())) {
                    safe = false;
                    break;
                }
            }

            if (safe) {
                if (m_springHoverPath != targetPath) {
                    m_springHoverPath = targetPath;
                    m_delegate->setSpringHoverPath(targetPath);
                    viewport()->update();
                    m_springTimer->start(AppSettings::instance().springLoadDelayMs());
                }
                event->acceptProposedAction();
                return;
            }
        }
    }

    resetSpringTimer();
    QTreeView::dragMoveEvent(event);
}

void ListView::dragLeaveEvent(QDragLeaveEvent* event) {
    resetSpringTimer();
    QTreeView::dragLeaveEvent(event);
}

void ListView::dropEvent(QDropEvent* event) {
    resetSpringTimer();
    QTreeView::dropEvent(event);
}

void ListView::resetSpringTimer() {
    if (m_springTimer && m_springTimer->isActive()) {
        m_springTimer->stop();
    }
    if (!m_springHoverPath.isEmpty()) {
        m_springHoverPath.clear();
        m_delegate->setSpringHoverPath(QString());
        viewport()->update();
    }
}

void ListView::onSpringTimeout() {
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        emit openPathRequested(target);
    }
}
