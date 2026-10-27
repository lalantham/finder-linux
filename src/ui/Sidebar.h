#pragma once

#include "VolumeManager.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStyledItemDelegate>

class SidebarItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit SidebarItemDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    void setSpringHoverPath(const QString& path) { m_springHoverPath = path; }
    [[nodiscard]] QString springHoverPath() const noexcept { return m_springHoverPath; }

private:
    QString m_springHoverPath;
};

class Sidebar : public QTreeWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);

    void refresh();
    void selectPath(const QString& path);

signals:
    void pathSelected(const QString& path);
    void openInNewTabRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void emptyTrashRequested();
    void volumeUnmounted(const QString& path);
    void itemsDroppedOnPath(const QStringList& sourcePaths, const QString& targetPath, Qt::DropAction action);
    void itemsDroppedOnTrash(const QStringList& sourcePaths);
    void tagSelected(const QString& tag);

public slots:
    void openManageSidebarDialog();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onSpringTimeout();

private:
    void resetSpringTimer();

    void populateFavorites();
    void populateLocations();
    void populateTags();

    SidebarItemDelegate* m_delegate{nullptr};
    QTimer* m_springTimer{nullptr};
    QString m_springHoverPath;
    QTreeWidgetItem* m_favoritesHeader{nullptr};
    QTreeWidgetItem* m_locationsHeader{nullptr};
    QTreeWidgetItem* m_tagsHeader{nullptr};
    QTreeWidgetItem* m_trashHeader{nullptr};
};
