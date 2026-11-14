#include "ListView.h"
#include "DesktopAppsManager.h"
#include "AppSettings.h"
#include <QHeaderView>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>

ListView::ListView(QWidget* parent)
    : QTreeView(parent)
{
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
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_delegate = new FileItemDelegate(this);
    setItemDelegate(m_delegate);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &ListView::onSpringTimeout);

    connect(this, &QTreeView::activated, this, &ListView::onItemActivated);
}

void ListView::setFileSystemModel(FileSystemModel* model)
{
    m_fsModel = model;
    setModel(model);

    // Initial column widths
    if (model) {
        setColumnWidth(FileSystemModel::ColName, 260);
        setColumnWidth(FileSystemModel::ColDateModified, 150);
        setColumnWidth(FileSystemModel::ColSize, 90);
        setColumnWidth(FileSystemModel::ColKind, 140);
    }
}

void ListView::onItemActivated(const QModelIndex& index)
{
    if (!index.isValid()) return;
    QString path = index.data(FileSystemModel::FilePathRole).toString();
    bool isDir = index.data(FileSystemModel::FileIsDirRole).toBool();

    if (isDir) {
        emit openPathRequested(path);
    } else {
        DesktopAppsManager::launchFile(path);
    }
}

void ListView::mouseDoubleClickEvent(QMouseEvent* event)
{
    QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        onItemActivated(idx);
    } else {
        QTreeView::mouseDoubleClickEvent(event);
    }
}

void ListView::keyPressEvent(QKeyEvent* event)
{
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

void ListView::contextMenuEvent(QContextMenuEvent* event)
{
    QModelIndex idx = indexAt(event->pos());
    QStringList paths;
    if (idx.isValid()) {
        if (!selectionModel()->isSelected(idx)) {
            setCurrentIndex(idx);
        }
        QModelIndexList selected = selectionModel()->selectedRows();
        for (const auto& i : selected) {
            QString p = i.data(FileSystemModel::FilePathRole).toString();
            if (!p.isEmpty() && !paths.contains(p)) paths.append(p);
        }
        if (paths.isEmpty()) {
            paths.append(idx.data(FileSystemModel::FilePathRole).toString());
        }
    }
    emit contextMenuRequested(event->globalPos(), paths);
}

void ListView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QTreeView::dragEnterEvent(event);
    }
}

void ListView::dragMoveEvent(QDragMoveEvent* event)
{
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
                if (url.isLocalFile()) {
                    QString src = url.toLocalFile();
                    if (targetPath == src || targetPath.startsWith(src + QLatin1Char('/'))) {
                        safe = false;
                        break;
                    }
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

void ListView::dragLeaveEvent(QDragLeaveEvent* event)
{
    resetSpringTimer();
    QTreeView::dragLeaveEvent(event);
}

void ListView::dropEvent(QDropEvent* event)
{
    resetSpringTimer();
    QTreeView::dropEvent(event);
}

void ListView::resetSpringTimer()
{
    if (m_springTimer && m_springTimer->isActive()) {
        m_springTimer->stop();
    }
    if (!m_springHoverPath.isEmpty()) {
        m_springHoverPath.clear();
        m_delegate->setSpringHoverPath(QString());
        viewport()->update();
    }
}

void ListView::onSpringTimeout()
{
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        emit openPathRequested(target);
    }
}
