#include "IconView.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>

IconView::IconView(QWidget* parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setWordWrap(true);
    setUniformItemSizes(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSpacing(12);

    m_delegate = new FileItemDelegate(this);
    setItemDelegate(m_delegate);

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
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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
    QMenu menu(this);

    if (idx.isValid()) {
        QString path = idx.data(FileSystemModel::FilePathRole).toString();
        bool isDir = idx.data(FileSystemModel::FileIsDirRole).toBool();

        auto* actOpen = menu.addAction(isDir ? QStringLiteral("Open") : QStringLiteral("Open With Default App"));
        connect(actOpen, &QAction::triggered, this, [this, idx]() {
            onItemActivated(idx);
        });

        auto* actQuickLook = menu.addAction(QStringLiteral("Quick Look"));
        connect(actQuickLook, &QAction::triggered, this, [this, path]() {
            emit quickLookRequested(path);
        });

        auto* actGetInfo = menu.addAction(QStringLiteral("Get Info"));
        connect(actGetInfo, &QAction::triggered, this, [this, path]() {
            emit getInfoRequested(path);
        });

        menu.addSeparator();
    }

    menu.exec(event->globalPos());
}
