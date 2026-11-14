#pragma once

#include "VolumeManager.h"
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariantMap>

class SidebarItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit SidebarItemDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

    void setSpringHoverPath(const QString& path) {
        m_springHoverPath = path;
    }
    [[nodiscard]] QString springHoverPath() const noexcept {
        return m_springHoverPath;
    }

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
    void itemsDroppedOnPath(const QStringList& sourcePaths, const QString& targetPath,
                            Qt::DropAction action);
    void itemsDroppedOnTrash(const QStringList& sourcePaths);
    void tagSelected(const QString& tag);
    void serverSelected(const QString& uri);
    void newSmartFolderRequested();
    void editSmartFolderRequested(const QString& name);
    void deleteSmartFolderRequested(const QString& name);

public slots:
    void openManageSidebarDialog();

protected:
    bool viewportEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onSpringTimeout();
    void checkVolumeChanges();
    void onUdisksInterfacesAdded(const QDBusObjectPath& path, const QVariantMap& interfaces);
    void onUdisksInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);

private:
    void resetSpringTimer();
    void mountDeviceAsync(const QString& device);
    void ejectVolumeAsync(const QString& path);

    void populateSmartLocations();
    void populateFavorites();
    void populateLocations();
    void populateServers();
    void populateTags();
    void populateSmartFolders();
    void requestVolumeScan(bool force = false);

    SidebarItemDelegate* m_delegate{nullptr};
    QTimer* m_springTimer{nullptr};
    QString m_springHoverPath;
    QFileSystemWatcher* m_volumeWatcher{nullptr};
    QTimer* m_volumePollTimer{nullptr};
    QTimer* m_volumeDebounceTimer{nullptr};
    QString m_lastVolumeFingerprint;
    QVector<VolumeEntry> m_lastVolumes;
    bool m_volumeScanRunning{false};
    QElapsedTimer m_volumeScanTimestamp;
    QTreeWidgetItem* m_favoritesHeader{nullptr};
    QTreeWidgetItem* m_locationsHeader{nullptr};
    QTreeWidgetItem* m_serversHeader{nullptr};
    QTreeWidgetItem* m_smartFoldersHeader{nullptr};
    QTreeWidgetItem* m_tagsHeader{nullptr};
    QTreeWidgetItem* m_trashHeader{nullptr};
};
