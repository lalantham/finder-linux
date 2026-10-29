#include "IconView.h"
#include "DesktopAppsManager.h"
#include "AppSettings.h"
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

IconView::IconView(QWidget* parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setWordWrap(true);
    setUniformItemSizes(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setMovement(QListView::Static);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);
    setSpacing(12);

    m_delegate = new FileItemDelegate(this);
    setItemDelegate(m_delegate);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &IconView::onSpringTimeout);

    connect(this, &QListView::activated, this, &IconView::onItemActivated);
}

void IconView::setFileSystemModel(FileSystemModel* model)
{
    m_fsModel = model;
    setModel(model);
}

void IconView::setIconSizeValue(int size)
{
    m_delegate->setIconSize(size);
    setIconSize(QSize(size, size));
    setGridSize(QSize(qMax(size + 36, 96), size + 48));
    doItemsLayout();
}

void IconView::onItemActivated(const QModelIndex& index)
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

void IconView::mouseDoubleClickEvent(QMouseEvent* event)
{
    QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        onItemActivated(idx);
    } else {
        QListView::mouseDoubleClickEvent(event);
    }
}

void IconView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        QModelIndex idx = currentIndex();
        if (idx.isValid()) {
            emit quickLookRequested(idx.data(FileSystemModel::FilePathRole).toString());
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
    QListView::keyPressEvent(event);
}

void IconView::contextMenuEvent(QContextMenuEvent* event)
{
    QModelIndex idx = indexAt(event->pos());
    QStringList paths;
    if (idx.isValid()) {
        if (!selectionModel()->isSelected(idx)) {
            setCurrentIndex(idx);
        }
        QModelIndexList selected = selectionModel()->selectedIndexes();
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

void IconView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QListView::dragEnterEvent(event);
    }
}

void IconView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!AppSettings::instance().springLoadedFoldersEnabled() || !event->mimeData()->hasUrls()) {
        resetSpringTimer();
        QListView::dragMoveEvent(event);
        return;
    }

    QModelIndex idx = indexAt(event->position().toPoint());
    if (idx.isValid() && idx.data(FileSystemModel::FileIsDirRole).toBool()) {
        QString targetPath = idx.data(FileSystemModel::FilePathRole).toString();

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

    resetSpringTimer();
    QListView::dragMoveEvent(event);
}

void IconView::dragLeaveEvent(QDragLeaveEvent* event)
{
    resetSpringTimer();
    QListView::dragLeaveEvent(event);
}

void IconView::dropEvent(QDropEvent* event)
{
    resetSpringTimer();
    QListView::dropEvent(event);
}

void IconView::resetSpringTimer()
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

void IconView::onSpringTimeout()
{
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        emit openPathRequested(target);
    }
}
