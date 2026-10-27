#include "ListView.h"
#include <QHeaderView>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>

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
    header()->setSectionsMovable(true);
    header()->setStretchLastSection(true);

    connect(this, &QTreeView::activated, this, &ListView::onItemActivated);
    connect(header(), &QHeaderView::sectionClicked, this, &ListView::onHeaderClicked);
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

void ListView::onHeaderClicked(int logicalIndex)
{
    if (!m_fsModel) return;
    Qt::SortOrder currentOrder = header()->sortIndicatorOrder();
    m_fsModel->sort(logicalIndex, currentOrder);
}

void ListView::onItemActivated(const QModelIndex& index)
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
