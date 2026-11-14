#include "IconView.h"
#include "AppSettings.h"
#include "DesktopAppsManager.h"
#include "PreviewManager.h"
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QTimer>
#include <QUrl>

IconView::IconView(QWidget* parent) : QListView(parent) {
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
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_delegate = new FileItemDelegate(this);
    setItemDelegate(m_delegate);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &IconView::onSpringTimeout);

    connect(&PreviewManager::instance(), &PreviewManager::thumbnailReady, this,
            [this](const QString&) { viewport()->update(); });

    connect(this, &QListView::activated, this, &IconView::onItemActivated);
}

void IconView::setFileSystemModel(FileSystemModel* model) {
    m_fsModel = model;
    setModel(model);

    if (model) {
        connect(model, &QAbstractItemModel::modelReset, this,
                [this]() { QTimer::singleShot(0, this, &IconView::applyIconPositions); });
        connect(model, &QAbstractItemModel::layoutChanged, this,
                [this]() { QTimer::singleShot(0, this, &IconView::applyIconPositions); });
        connect(model, &QAbstractItemModel::rowsInserted, this,
                [this]() { QTimer::singleShot(0, this, &IconView::applyIconPositions); });
        connect(model, &FileSystemModel::sortChanged, this, [this]() {
            if (m_manualArrangement) {
                m_iconPositions.clear();
                AppSettings::instance().clearIconPositions(m_folderPath);
                QTimer::singleShot(0, this, &IconView::alignToGrid);
            }
        });
    }
}

void IconView::setFolderPath(const QString& path) {
    if (m_manualArrangement) {
        saveIconPositions();
    }
    if (path.isEmpty() || path.startsWith(QStringLiteral("tag://")) ||
        path.startsWith(QStringLiteral("search://"))) {
        m_folderPath.clear();
        m_iconPositions.clear();
        setManualArrangement(false);
        return;
    }
    const QString clean = QDir::cleanPath(path);
    if (m_folderPath == clean)
        return;
    m_folderPath = clean;
    m_iconPositions.clear();
    const QVariantMap saved = AppSettings::instance().iconPositions(m_folderPath);
    for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
        m_iconPositions.insert(it.key(), it.value().toPoint());
    }
    QTimer::singleShot(0, this, &IconView::applyIconPositions);
}

void IconView::setManualArrangement(bool enabled) {
    if (m_manualArrangement == enabled)
        return;
    if (m_manualArrangement) {
        saveIconPositions();
    }
    m_manualArrangement = enabled;
    setMovement(enabled ? QListView::Free : QListView::Static);
    if (enabled) {
        QTimer::singleShot(0, this, &IconView::applyIconPositions);
    } else {
        updateLayoutMetrics();
    }
}

void IconView::setIconSizeValue(int size) {
    m_delegate->setIconSize(size);
    updateLayoutMetrics();
}

void IconView::applyViewOptions(const FolderViewOptions& options) {
    setManualArrangement(options.manualArrangement);
    m_delegate->setIconSize(options.iconSize);
    m_delegate->setTextSize(options.textSize);
    m_delegate->setLabelPosition(options.labelPosition == 1
                                     ? FileItemDelegate::LabelPosition::Right
                                     : FileItemDelegate::LabelPosition::Bottom);
    m_delegate->setShowItemInfo(options.showItemInfo);
    m_delegate->setShowIconPreview(options.showIconPreview);
    setSpacing(options.gridSpacing);
    updateLayoutMetrics();
}

void IconView::updateLayoutMetrics() {
    const int size = m_delegate->iconSize();
    setIconSize(QSize(size, size));
    setGridSize(m_delegate->iconGridSize());
    doItemsLayout();
}

void IconView::cleanUp() {
    alignToGrid();
}

void IconView::alignToGrid() {
    if (!m_fsModel)
        return;
    m_iconPositions.clear();
    AppSettings::instance().clearIconPositions(m_folderPath);
    const QListView::Movement oldMovement = movement();
    setMovement(QListView::Static);
    doItemsLayout();
    if (m_manualArrangement) {
        setMovement(QListView::Free);
    } else {
        setMovement(oldMovement);
    }
    saveIconPositions();
    viewport()->update();
}

void IconView::applyIconPositions() {
    if (!m_manualArrangement || !m_fsModel || m_applyingPositions || m_iconPositions.isEmpty()) {
        return;
    }
    m_applyingPositions = true;
    for (int row = 0; row < m_fsModel->rowCount(); ++row) {
        const QModelIndex index = m_fsModel->index(row, FileSystemModel::ColName);
        const QString path = index.data(FileSystemModel::FilePathRole).toString();
        const auto it = m_iconPositions.constFind(path);
        if (it != m_iconPositions.cend()) {
            setPositionForIndex(it.value(), index);
        }
    }
    m_applyingPositions = false;
}

void IconView::saveIconPositions() {
    if (!m_manualArrangement || !m_fsModel || m_applyingPositions)
        return;
    QVariantMap saved;
    for (int row = 0; row < m_fsModel->rowCount(); ++row) {
        const QModelIndex index = m_fsModel->index(row, FileSystemModel::ColName);
        const QString path = index.data(FileSystemModel::FilePathRole).toString();
        const QRect rect = rectForIndex(index);
        if (!path.isEmpty() && rect.isValid() && !rect.isEmpty()) {
            saved.insert(path, rect.topLeft());
        }
    }
    if (saved.isEmpty()) {
        return;
    }

    bool changed = saved.size() != m_iconPositions.size();
    for (auto it = saved.cbegin(); !changed && it != saved.cend(); ++it) {
        changed = !m_iconPositions.contains(it.key()) ||
                  m_iconPositions.value(it.key()) != it.value().toPoint();
    }
    if (changed) {
        m_iconPositions.clear();
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
            m_iconPositions.insert(it.key(), it.value().toPoint());
        }
        AppSettings::instance().setIconPositions(m_folderPath, saved);
    }
}

void IconView::mouseReleaseEvent(QMouseEvent* event) {
    QListView::mouseReleaseEvent(event);
    if (m_manualArrangement) {
        saveIconPositions();
    }
}

void IconView::onItemActivated(const QModelIndex& index) {
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

void IconView::mouseDoubleClickEvent(QMouseEvent* event) {
    QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        onItemActivated(idx);
    } else {
        QListView::mouseDoubleClickEvent(event);
    }
}

void IconView::keyPressEvent(QKeyEvent* event) {
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
    QListView::keyPressEvent(event);
}

void IconView::contextMenuEvent(QContextMenuEvent* event) {
    QModelIndex idx = indexAt(event->pos());
    QStringList paths;
    if (idx.isValid()) {
        if (!selectionModel()->isSelected(idx)) {
            setCurrentIndex(idx);
        }
        QModelIndexList selected = selectionModel()->selectedIndexes();
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

void IconView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        QListView::dragEnterEvent(event);
    }
}

void IconView::dragMoveEvent(QDragMoveEvent* event) {
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

void IconView::dragLeaveEvent(QDragLeaveEvent* event) {
    resetSpringTimer();
    QListView::dragLeaveEvent(event);
}

void IconView::dropEvent(QDropEvent* event) {
    resetSpringTimer();
    QListView::dropEvent(event);
}

void IconView::resetSpringTimer() {
    if (m_springTimer && m_springTimer->isActive()) {
        m_springTimer->stop();
    }
    if (!m_springHoverPath.isEmpty()) {
        m_springHoverPath.clear();
        m_delegate->setSpringHoverPath(QString());
        viewport()->update();
    }
}

void IconView::onSpringTimeout() {
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        emit openPathRequested(target);
    }
}
