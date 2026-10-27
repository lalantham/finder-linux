#pragma once

#include "VolumeManager.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>

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

public slots:
    void openManageSidebarDialog();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    void populateFavorites();
    void populateLocations();

    QTreeWidgetItem* m_favoritesHeader{nullptr};
    QTreeWidgetItem* m_locationsHeader{nullptr};
    QTreeWidgetItem* m_trashHeader{nullptr};
};
