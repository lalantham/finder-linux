#include "Sidebar.h"
#include "AppSettings.h"
#include "DesktopAppsManager.h"
#include "FileItem.h"
#include "SidebarSettingsDialog.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include "XdgTrash.h"
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDBusConnection>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHelpEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

static constexpr int PathRole = Qt::UserRole + 10;
static constexpr int IsCustomFavRole = Qt::UserRole + 11;
static constexpr int IsTrashRole = Qt::UserRole + 12;
static constexpr int IsLocationRole = Qt::UserRole + 13;
static constexpr int IsStandardFavRole = Qt::UserRole + 14;
static constexpr int HeaderTypeRole = Qt::UserRole + 15; // 1 = Favorites, 2 = Locations, 3 = Tags
static constexpr int IsTagRole = Qt::UserRole + 16;
static constexpr int TagNameRole = Qt::UserRole + 17;
static constexpr int TotalBytesRole = Qt::UserRole + 18;
static constexpr int AvailableBytesRole = Qt::UserRole + 19;
static constexpr int DeviceRole = Qt::UserRole + 20;
static constexpr int IsMountedRole = Qt::UserRole + 21;
static constexpr int IsUsbRole = Qt::UserRole + 22;
static constexpr int IsSmartFolderRole = Qt::UserRole + 23;
static constexpr int SmartFolderNameRole = Qt::UserRole + 24;

namespace {

QString volumeToolTip(const QString& name, const QString& device, qint64 totalBytes,
                      qint64 availableBytes, bool isMounted) {
    const QString safeName = name.toHtmlEscaped();
    if (!isMounted || totalBytes <= 0) {
        return QStringLiteral("%1 (Unmounted — click to open)").arg(safeName);
    }

    const qint64 usedBytes = qMax<qint64>(0, totalBytes - availableBytes);
    const double usedPercent =
        static_cast<double>(usedBytes) * 100.0 / static_cast<double>(totalBytes);

    QString tip = QStringLiteral("<b>%1</b><br/>").arg(safeName);
    tip += QStringLiteral("Used: %1 of %2 (%3%)<br/>")
               .arg(FileItem::formatSize(usedBytes), FileItem::formatSize(totalBytes),
                    QString::number(usedPercent, 'f', 1));
    tip += QStringLiteral("Free: %1").arg(FileItem::formatSize(availableBytes));
    if (!device.isEmpty()) {
        tip += QStringLiteral("<br/>Device: %1").arg(device.toHtmlEscaped());
    }
    return tip;
}

} // namespace

SidebarItemDelegate::SidebarItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize SidebarItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    int headerType = index.data(HeaderTypeRole).toInt();
    if (headerType > 0) {
        return QSize(option.rect.width(), 26);
    }
    qint64 totalBytes = index.data(TotalBytesRole).toLongLong();
    if (totalBytes > 0) {
        return QSize(option.rect.width(), 28);
    }
    return QSize(option.rect.width(), 24);
}

void SidebarItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    int headerType = index.data(HeaderTypeRole).toInt();
    const auto& c = ThemeManager::instance().colors();

    if (headerType > 0) {
        QString title = index.data(Qt::DisplayRole).toString();
        QRect textRect = option.rect.adjusted(8, 4, -8, 0);
        QFont hf = option.font;
        hf.setBold(true);
        hf.setPointSize(9);
        painter->setFont(hf);
        painter->setPen(c.subText);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
        painter->restore();
        return;
    }

    bool isSelected = option.state & QStyle::State_Selected;
    bool isHovered = option.state & QStyle::State_MouseOver;
    QString itemPath = index.data(PathRole).toString();
    bool isSpringHover = (!m_springHoverPath.isEmpty() && m_springHoverPath == itemPath);

    // Draw background pill (compact margins)
    QRect itemRect = option.rect.adjusted(3, 1, -3, -1);
    if (isSpringHover) {
        painter->setPen(QPen(c.accent, 1.5));
        painter->setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 50));
        painter->drawRoundedRect(itemRect, 4, 4);
    } else if (isSelected) {
        painter->setPen(Qt::NoPen);
        QColor selBg =
            c.isDark ? QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 90) : c.accent;
        painter->setBrush(selBg);
        painter->drawRoundedRect(itemRect, 4, 4);
    } else if (isHovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(c.isDark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 10));
        painter->drawRoundedRect(itemRect, 4, 4);
    }

    // Draw 16x16 icon
    QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    int iconX = itemRect.left() + 5;
    int iconY = itemRect.top() + (itemRect.height() - 16) / 2;
    QRect iconRect(iconX, iconY, 16, 16);
    if (!icon.isNull()) {
        icon.paint(painter, iconRect);
    }

    // Draw label
    QString text = index.data(Qt::DisplayRole).toString();
    qint64 totalBytes = index.data(TotalBytesRole).toLongLong();
    qint64 availBytes = index.data(AvailableBytesRole).toLongLong();
    bool isMounted =
        index.data(IsMountedRole).isValid() ? index.data(IsMountedRole).toBool() : true;
    bool hasDiskBar = isMounted && (totalBytes > 0);

    QFont font = option.font;
    font.setPointSize(10);
    painter->setFont(font);
    painter->setPen(
        isSelected
            ? Qt::white
            : (isMounted ? c.text : QColor(c.text.red(), c.text.green(), c.text.blue(), 170)));

    int textX = iconX + 22;
    int textW = itemRect.right() - textX - 6;

    if (hasDiskBar) {
        QRect labelRect(textX, itemRect.top() + 2, textW, 14);
        QFontMetrics fm(font);
        QString elided = fm.elidedText(text, Qt::ElideMiddle, textW);
        painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

        int barY = itemRect.bottom() - 4;
        int barH = 2;
        int barW = qMax(20, textW - 8);
        QRect barBgRect(textX, barY, barW, barH);

        painter->setPen(Qt::NoPen);
        painter->setBrush(c.isDark ? QColor(58, 62, 68) : QColor(200, 203, 207));
        painter->drawRoundedRect(barBgRect, 1, 1);

        double usedRatio = qBound(
            0.0, 1.0 - static_cast<double>(availBytes) / static_cast<double>(totalBytes), 1.0);
        int usedW = static_cast<int>(barW * usedRatio);
        if (usedW > 0) {
            QRect usedRect(textX, barY, usedW, barH);
            painter->setBrush(c.accent);
            painter->drawRoundedRect(usedRect, 1, 1);
        }
    } else {
        QRect labelRect(textX, itemRect.top(), textW, itemRect.height());
        QFontMetrics fm(font);
        QString elided = fm.elidedText(text, Qt::ElideMiddle, textW);
        painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

    painter->restore();
}

Sidebar::Sidebar(QWidget* parent) : QTreeWidget(parent) {
    setObjectName(QStringLiteral("sidebar"));
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(0);
    setIconSize(QSize(18, 18));
    m_delegate = new SidebarItemDelegate(this);
    setItemDelegate(m_delegate);
    setAnimated(true);
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Sidebar"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &Sidebar::onSpringTimeout);

    m_volumeWatcher = new QFileSystemWatcher(this);
    QStringList watchDirs;
    if (QDir(QStringLiteral("/run/media")).exists()) {
        watchDirs << QStringLiteral("/run/media");
        QString userMedia = QStringLiteral("/run/media/") + QString::fromUtf8(qgetenv("USER"));
        if (QDir(userMedia).exists()) {
            watchDirs << userMedia;
        }
    }
    if (QDir(QStringLiteral("/media")).exists()) {
        watchDirs << QStringLiteral("/media");
    }
    if (!watchDirs.isEmpty()) {
        m_volumeWatcher->addPaths(watchDirs);
        connect(m_volumeWatcher, &QFileSystemWatcher::directoryChanged, this, &Sidebar::refresh);
    }

    m_volumePollTimer = new QTimer(this);
    m_volumePollTimer->setInterval(3000);
    connect(m_volumePollTimer, &QTimer::timeout, this, &Sidebar::checkVolumeChanges);
    m_volumePollTimer->start();

    // Refresh promptly on hotplug via udisks2 signals; polling remains as a
    // fallback for environments without a system bus.
    m_volumeDebounceTimer = new QTimer(this);
    m_volumeDebounceTimer->setSingleShot(true);
    m_volumeDebounceTimer->setInterval(400);
    connect(m_volumeDebounceTimer, &QTimer::timeout, this, &Sidebar::refresh);

    if (QDBusConnection::systemBus().isConnected()) {
        QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.UDisks2"), QStringLiteral("/org/freedesktop/UDisks2"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("InterfacesAdded"),
            this, SLOT(onUdisksInterfacesAdded(QDBusObjectPath, QVariantMap)));
        QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.UDisks2"), QStringLiteral("/org/freedesktop/UDisks2"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"),
            QStringLiteral("InterfacesRemoved"), this,
            SLOT(onUdisksInterfacesRemoved(QDBusObjectPath, QStringList)));
    }

    connect(this, &QTreeWidget::itemClicked, this, &Sidebar::onItemClicked);
    connect(this, &QTreeWidget::itemActivated, this, &Sidebar::onItemClicked);
    connect(&TagManager::instance(), &TagManager::tagsChanged, this, &Sidebar::refresh);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this]() { viewport()->update(); });

    refresh();
}

void Sidebar::refresh() {
    QString currentSelected;
    QTreeWidgetItem* curr = currentItem();
    if (curr) {
        currentSelected = curr->data(0, PathRole).toString();
    }

    clear();

    populateFavorites();
    populateSmartFolders();
    populateLocations();
    populateTags();
    requestVolumeScan();

    expandAll();

    if (!currentSelected.isEmpty()) {
        selectPath(currentSelected);
    }
}

void Sidebar::requestVolumeScan(bool force) {
    if (m_volumeScanRunning) {
        return;
    }
    if (!force && m_volumeScanTimestamp.isValid() && m_volumeScanTimestamp.elapsed() < 10000) {
        return;
    }

    m_volumeScanRunning = true;
    auto* watcher = new QFutureWatcher<QVector<VolumeEntry>>(this);
    connect(watcher, &QFutureWatcher<QVector<VolumeEntry>>::finished, this, [this, watcher]() {
        m_volumeScanRunning = false;
        m_lastVolumes = watcher->result();
        watcher->deleteLater();
        m_volumeScanTimestamp.start();
        refresh();
    });
    watcher->setFuture(QtConcurrent::run([]() { return VolumeManager::mountedVolumes(); }));
}

void Sidebar::populateFavorites() {
    m_favoritesHeader = new QTreeWidgetItem(this);
    m_favoritesHeader->setText(0, QStringLiteral("Favorites"));
    m_favoritesHeader->setData(0, HeaderTypeRole, 1);
    QFont hf = m_favoritesHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(9);
    m_favoritesHeader->setFont(0, hf);
    m_favoritesHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_favoritesHeader->setFlags(Qt::ItemIsEnabled);

    auto addFavItem = [this](const QString& name, const QString& path, const QString& themeIconName,
                             bool isCustom) {
        if (!QDir(path).exists())
            return;

        auto* item = new QTreeWidgetItem(m_favoritesHeader);
        item->setText(0, name);
        item->setData(0, PathRole, path);
        item->setData(0, IsCustomFavRole, isCustom);
        item->setData(0, IsStandardFavRole, !isCustom);

        QFileIconProvider iconProv;
        QIcon icon = QIcon::fromTheme(
            themeIconName,
            QIcon::fromTheme(QStringLiteral("folder"), iconProv.icon(QFileIconProvider::Folder)));
        item->setIcon(0, icon);
    };

    const QStringList orderedFavs = AppSettings::instance().favoriteOrder();
    const QString homePath = QDir::homePath();
    const QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString downPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString picsPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString musicPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString vidsPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString trashPath = XdgTrash::trashFilesDirectory();

    for (const auto& path : orderedFavs) {
        if (!QDir(path).exists())
            continue;
        if (AppSettings::instance().isFavoriteHidden(path))
            continue;

        if (path == homePath) {
            addFavItem(QStringLiteral("Home"), path, QStringLiteral("user-home"), false);
        } else if (path == desktopPath) {
            addFavItem(QStringLiteral("Desktop"), path, QStringLiteral("user-desktop"), false);
        } else if (path == docsPath) {
            addFavItem(QStringLiteral("Documents"), path, QStringLiteral("folder-documents"),
                       false);
        } else if (path == downPath) {
            addFavItem(QStringLiteral("Downloads"), path, QStringLiteral("folder-download"), false);
        } else if (path == picsPath) {
            addFavItem(QStringLiteral("Pictures"), path, QStringLiteral("folder-pictures"), false);
        } else if (path == musicPath) {
            addFavItem(QStringLiteral("Music"), path, QStringLiteral("folder-music"), false);
        } else if (path == vidsPath) {
            addFavItem(QStringLiteral("Videos"), path, QStringLiteral("folder-videos"), false);
        } else {
            QFileInfo fi(path);
            addFavItem(fi.fileName().isEmpty() ? path : fi.fileName(), path,
                       QStringLiteral("folder"), true);
        }
    }

    // Add Trash to Favorites right after standard items if not hidden
    bool trashHidden = AppSettings::instance().isLocationHidden(QStringLiteral("Trash")) ||
                       AppSettings::instance().isLocationHidden(trashPath) ||
                       AppSettings::instance().isFavoriteHidden(QStringLiteral("Trash"));
    if (!trashHidden) {
        QFileIconProvider iconProv;
        auto* item = new QTreeWidgetItem(m_favoritesHeader);
        item->setText(0, QStringLiteral("Trash"));
        item->setData(0, PathRole, trashPath);
        item->setData(0, IsTrashRole, true);
        item->setData(0, IsStandardFavRole, true);
        item->setIcon(
            0, QIcon::fromTheme(
                   QStringLiteral("user-trash"),
                   QIcon::fromTheme(QStringLiteral("trash-empty"),
                                    QIcon::fromTheme(QStringLiteral("edit-delete"),
                                                     iconProv.icon(QFileIconProvider::Trashcan)))));
    }
}

void Sidebar::populateSmartFolders() {
    m_smartFoldersHeader = nullptr;
    const QList<SmartFolder> folders = AppSettings::instance().smartFolders();
    if (folders.isEmpty()) {
        return;
    }

    m_smartFoldersHeader = new QTreeWidgetItem(this);
    m_smartFoldersHeader->setText(0, QStringLiteral("Smart Folders"));
    m_smartFoldersHeader->setData(0, HeaderTypeRole, 4);
    QFont hf = m_smartFoldersHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(9);
    m_smartFoldersHeader->setFont(0, hf);
    m_smartFoldersHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_smartFoldersHeader->setFlags(Qt::ItemIsEnabled);

    for (const SmartFolder& folder : folders) {
        auto* item = new QTreeWidgetItem(m_smartFoldersHeader);
        item->setText(0, folder.name);
        item->setData(0, PathRole, QStringLiteral("smart://") + folder.name);
        item->setData(0, IsSmartFolderRole, true);
        item->setData(0, SmartFolderNameRole, folder.name);
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-search"),
                                          QIcon::fromTheme(QStringLiteral("edit-find"))));
        item->setToolTip(0, folder.filter.query.isEmpty()
                                ? QStringLiteral("Smart Folder")
                                : QStringLiteral("Search: %1").arg(folder.filter.query));
    }
}

void Sidebar::populateLocations() {
    m_locationsHeader = new QTreeWidgetItem(this);
    m_locationsHeader->setText(0, QStringLiteral("Locations"));
    m_locationsHeader->setData(0, HeaderTypeRole, 2);
    QFont hf = m_locationsHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(9);
    m_locationsHeader->setFont(0, hf);
    m_locationsHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_locationsHeader->setFlags(Qt::ItemIsEnabled);

    QFileIconProvider iconProv;
    const QVector<VolumeEntry> volumes = m_lastVolumes;

    struct LocData {
        QString name;
        QIcon icon;
        qint64 totalBytes{0};
        qint64 availableBytes{0};
        QString device;
        bool isMounted{true};
        bool isUsb{false};
    };
    QMap<QString, LocData> map;
    for (const auto& vol : volumes) {
        map[vol.path] = {vol.name,   vol.icon,      vol.totalBytes, vol.availableBytes,
                         vol.device, vol.isMounted, vol.isUsb};
    }

    QSet<QString> addedPaths;
    const QStringList orderedLocs = AppSettings::instance().locationOrder();
    for (const auto& path : orderedLocs) {
        if (!map.contains(path))
            continue;
        if (AppSettings::instance().isLocationHidden(path))
            continue;

        const auto& data = map[path];
        auto* item = new QTreeWidgetItem(m_locationsHeader);
        item->setText(0, data.name);
        item->setData(0, PathRole, path);
        item->setData(0, DeviceRole, data.device);
        item->setData(0, IsLocationRole, true);
        item->setData(0, IsMountedRole, data.isMounted);
        item->setData(0, IsUsbRole, data.isUsb);
        item->setData(0, TotalBytesRole, data.totalBytes);
        item->setData(0, AvailableBytesRole, data.availableBytes);
        item->setIcon(0, data.icon);
        item->setToolTip(0, volumeToolTip(data.name, data.device, data.totalBytes,
                                          data.availableBytes, data.isMounted));
        addedPaths.insert(path);
    }

    // Add any newly connected or unmounted volumes not yet in orderedLocs
    for (const auto& vol : volumes) {
        if (addedPaths.contains(vol.path))
            continue;
        if (AppSettings::instance().isLocationHidden(vol.path))
            continue;

        auto* item = new QTreeWidgetItem(m_locationsHeader);
        item->setText(0, vol.name);
        item->setData(0, PathRole, vol.path);
        item->setData(0, DeviceRole, vol.device);
        item->setData(0, IsLocationRole, true);
        item->setData(0, IsMountedRole, vol.isMounted);
        item->setData(0, IsUsbRole, vol.isUsb);
        item->setData(0, TotalBytesRole, vol.totalBytes);
        item->setData(0, AvailableBytesRole, vol.availableBytes);
        item->setIcon(0, vol.icon);
        item->setToolTip(0, volumeToolTip(vol.name, vol.device, vol.totalBytes, vol.availableBytes,
                                          vol.isMounted));
        addedPaths.insert(vol.path);
    }
}

void Sidebar::populateTags() {
    m_tagsHeader = new QTreeWidgetItem(this);
    m_tagsHeader->setText(0, QStringLiteral("Tags"));
    m_tagsHeader->setData(0, HeaderTypeRole, 3);
    QFont hf = m_tagsHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(9);
    m_tagsHeader->setFont(0, hf);
    m_tagsHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_tagsHeader->setFlags(Qt::ItemIsEnabled);

    for (const auto& tagInfo : TagManager::standardTags()) {
        auto* item = new QTreeWidgetItem(m_tagsHeader);
        int count = TagManager::instance().tagCount(tagInfo.name);
        QString label = tagInfo.name;
        if (count > 0) {
            label += QStringLiteral(" (%1)").arg(count);
        }
        item->setText(0, label);
        item->setData(0, IsTagRole, true);
        item->setData(0, TagNameRole, tagInfo.name);
        item->setIcon(0, TagManager::tagDotIcon(tagInfo.color, 14));
    }
}

namespace {
struct VolumeTaskResult {
    QString path;
    QString error;
    bool ok{false};
};
} // namespace

void Sidebar::mountDeviceAsync(const QString& device) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto* watcher = new QFutureWatcher<VolumeTaskResult>(this);
    connect(watcher, &QFutureWatcher<VolumeTaskResult>::finished, this, [this, watcher, device]() {
        QApplication::restoreOverrideCursor();
        const VolumeTaskResult result = watcher->result();
        watcher->deleteLater();
        if (!result.path.isEmpty()) {
            m_volumeScanTimestamp.invalidate();
            refresh();
            emit pathSelected(result.path);
        } else {
            QMessageBox::warning(this, QStringLiteral("Cannot Mount Device"),
                                 result.error.isEmpty()
                                     ? QStringLiteral("Failed to mount %1").arg(device)
                                     : result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run([device]() {
        VolumeTaskResult result;
        result.path = VolumeManager::mountDevice(device, &result.error);
        result.ok = !result.path.isEmpty();
        return result;
    }));
}

void Sidebar::ejectVolumeAsync(const QString& path) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto* watcher = new QFutureWatcher<VolumeTaskResult>(this);
    connect(watcher, &QFutureWatcher<VolumeTaskResult>::finished, this, [this, watcher, path]() {
        QApplication::restoreOverrideCursor();
        const VolumeTaskResult result = watcher->result();
        watcher->deleteLater();
        if (result.ok) {
            emit volumeUnmounted(path);
            m_volumeScanTimestamp.invalidate();
            refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Cannot Eject"),
                                 result.error.isEmpty()
                                     ? QStringLiteral("Failed to eject %1").arg(path)
                                     : result.error);
        }
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        VolumeTaskResult result;
        result.ok = VolumeManager::ejectVolume(path, &result.error);
        return result;
    }));
}

void Sidebar::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item)
        return;
    if (item->data(0, IsTagRole).toBool()) {
        QString tag = item->data(0, TagNameRole).toString();
        emit tagSelected(tag);
        return;
    }
    if (item->data(0, IsLocationRole).toBool()) {
        bool isMounted =
            item->data(0, IsMountedRole).isValid() ? item->data(0, IsMountedRole).toBool() : true;
        if (!isMounted) {
            QString dev = item->data(0, DeviceRole).toString();
            if (dev.isEmpty())
                dev = item->data(0, PathRole).toString();
            mountDeviceAsync(dev);
            return;
        }
    }
    QString path = item->data(0, PathRole).toString();
    if (!path.isEmpty()) {
        emit pathSelected(path);
    }
}

void Sidebar::selectPath(const QString& path) {
    if (path.startsWith(QStringLiteral("smart://"))) {
        const QString name = path.mid(8);
        for (int i = 0; i < topLevelItemCount(); ++i) {
            QTreeWidgetItem* top = topLevelItem(i);
            for (int j = 0; j < top->childCount(); ++j) {
                QTreeWidgetItem* child = top->child(j);
                if (child->data(0, IsSmartFolderRole).toBool() &&
                    child->data(0, SmartFolderNameRole)
                            .toString()
                            .compare(name, Qt::CaseInsensitive) == 0) {
                    setCurrentItem(child);
                    return;
                }
            }
        }
        return;
    }

    if (path.startsWith(QStringLiteral("tag://"))) {
        QString tagName = path.mid(6);
        for (int i = 0; i < topLevelItemCount(); ++i) {
            QTreeWidgetItem* top = topLevelItem(i);
            for (int j = 0; j < top->childCount(); ++j) {
                QTreeWidgetItem* child = top->child(j);
                if (child->data(0, IsTagRole).toBool()) {
                    if (child->data(0, TagNameRole)
                            .toString()
                            .compare(tagName, Qt::CaseInsensitive) == 0) {
                        setCurrentItem(child);
                        return;
                    }
                }
            }
        }
        return;
    }

    QString clean = QDir::cleanPath(path);
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j) {
            QTreeWidgetItem* child = top->child(j);
            if (child->data(0, PathRole).toString() == clean) {
                setCurrentItem(child);
                return;
            }
        }
    }
}

bool Sidebar::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        QTreeWidgetItem* item = itemAt(help->pos());
        if (item && item->data(0, IsLocationRole).toBool()) {
            const QString path = item->data(0, PathRole).toString();
            const QString device = item->data(0, DeviceRole).toString();
            const bool isMounted = item->data(0, IsMountedRole).isValid()
                                       ? item->data(0, IsMountedRole).toBool()
                                       : true;

            qint64 totalBytes = item->data(0, TotalBytesRole).toLongLong();
            qint64 availableBytes = item->data(0, AvailableBytesRole).toLongLong();

            // Re-query on hover so the numbers are current even when the
            // sidebar has not been refreshed since the last file operation.
            if (isMounted && !path.isEmpty()) {
                const QStorageInfo storage(path);
                if (storage.isValid() && storage.isReady()) {
                    totalBytes = storage.bytesTotal();
                    availableBytes = storage.bytesAvailable();
                    item->setData(0, TotalBytesRole, totalBytes);
                    item->setData(0, AvailableBytesRole, availableBytes);
                }
            }

            item->setToolTip(
                0, volumeToolTip(item->text(0), device, totalBytes, availableBytes, isMounted));
        }
    }

    return QTreeWidget::viewportEvent(event);
}

void Sidebar::contextMenuEvent(QContextMenuEvent* event) {
    QPoint vpPos = viewport()->mapFromGlobal(event->globalPos());
    QModelIndex idx = indexAt(vpPos);
    QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;
    if (!item) {
        item = itemAt(vpPos);
    }
    if (!item) {
        item = itemAt(event->pos());
    }

    if (item) {
        setCurrentItem(item);
    }

    QMenu menu(this);

    if (item) {
        int headerType = item->data(0, HeaderTypeRole).toInt();

        // 1. Right clicked "Favorites" Header
        if (headerType == 1) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")),
                           QStringLiteral("Pin Folder..."), this, [this]() {
                               QString dir = QFileDialog::getExistingDirectory(
                                   this, QStringLiteral("Select Folder to Pin to Sidebar"),
                                   QDir::homePath());
                               if (!dir.isEmpty()) {
                                   AppSettings::instance().addFavorite(dir);
                                   refresh();
                               }
                           });

            QStringList hiddenFavs = AppSettings::instance().hiddenFavorites();
            if (!hiddenFavs.isEmpty()) {
                auto* unhideMenu = menu.addMenu(QStringLiteral("Unhide"));
                for (const auto& hf : hiddenFavs) {
                    QFileInfo fi(hf);
                    QString label = fi.fileName().isEmpty() ? hf : fi.fileName();
                    unhideMenu->addAction(label, this, [this, hf]() {
                        AppSettings::instance().unhideFavorite(hf);
                        refresh();
                    });
                }
                unhideMenu->addSeparator();
                unhideMenu->addAction(QStringLiteral("Unhide All Favorites"), this, [this]() {
                    AppSettings::instance().unhideAllFavorites();
                    refresh();
                });
            }

            menu.addSeparator();
            menu.addAction(QStringLiteral("Manage Sidebar..."), this,
                           &Sidebar::openManageSidebarDialog);
            menu.exec(event->globalPos());
            return;
        }

        // 2. Right clicked "Locations" Header
        if (headerType == 2) {
            QStringList hiddenLocs = AppSettings::instance().hiddenLocations();
            if (!hiddenLocs.isEmpty()) {
                auto* unhideMenu = menu.addMenu(QStringLiteral("Unhide"));
                for (const auto& hl : hiddenLocs) {
                    QFileInfo fi(hl);
                    QString label = fi.fileName().isEmpty() ? hl : fi.fileName();
                    unhideMenu->addAction(label, this, [this, hl]() {
                        AppSettings::instance().unhideLocation(hl);
                        refresh();
                    });
                }
                unhideMenu->addSeparator();
                unhideMenu->addAction(QStringLiteral("Unhide All Locations"), this, [this]() {
                    AppSettings::instance().unhideAllLocations();
                    refresh();
                });
            }

            menu.addSeparator();
            menu.addAction(QStringLiteral("Manage Sidebar..."), this,
                           &Sidebar::openManageSidebarDialog);
            menu.exec(event->globalPos());
            return;
        }

        if (headerType == 4) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")),
                           QStringLiteral("New Smart Folder..."), this,
                           &Sidebar::newSmartFolderRequested);
            menu.exec(event->globalPos());
            return;
        }

        // 3. Right clicked on Smart Folder item
        if (item->data(0, IsSmartFolderRole).toBool()) {
            const QString name = item->data(0, SmartFolderNameRole).toString();
            auto* openAct = menu.addAction(
                QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this,
                [this, name]() { emit pathSelected(QStringLiteral("smart://") + name); });
            QFont f = openAct->font();
            f.setBold(true);
            openAct->setFont(f);

            menu.addAction(QStringLiteral("Open in New Tab"), this, [this, name]() {
                emit openInNewTabRequested(QStringLiteral("smart://") + name);
            });
            menu.addAction(QStringLiteral("Open in New Window"), this, [name]() {
                QProcess::startDetached(
                    QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--new-window"), QStringLiteral("smart://") + name});
            });
            menu.addSeparator();
            menu.addAction(QStringLiteral("Edit Smart Folder..."), this,
                           [this, name]() { emit editSmartFolderRequested(name); });
            menu.addAction(QStringLiteral("Delete Smart Folder"), this,
                           [this, name]() { emit deleteSmartFolderRequested(name); });
            menu.exec(event->globalPos());
            return;
        }

        // 4. Right clicked on Tag item
        if (item->data(0, IsTagRole).toBool()) {
            QString tag = item->data(0, TagNameRole).toString();
            auto* openAct = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                                           QStringLiteral("Open"), this,
                                           [this, tag]() { emit tagSelected(tag); });
            QFont f = openAct->font();
            f.setBold(true);
            openAct->setFont(f);

            menu.addSeparator();
            int count = TagManager::instance().tagCount(tag);
            auto* clearAct = menu.addAction(
                QStringLiteral("Clear All \"%1\" Tags (%2 Files)").arg(tag).arg(count), this,
                [this, tag]() {
                    QStringList files = TagManager::instance().filesForTag(tag);
                    for (const auto& f : files) {
                        TagManager::instance().removeTag(f, tag);
                    }
                    refresh();
                });
            if (count == 0)
                clearAct->setEnabled(false);

            menu.exec(event->globalPos());
            return;
        }

        // 5. Right clicked specific item
        bool isCustom = item->data(0, IsCustomFavRole).toBool();
        bool isStandard = item->data(0, IsStandardFavRole).toBool();
        bool isTrash = item->data(0, IsTrashRole).toBool();
        bool isLocation = item->data(0, IsLocationRole).toBool();
        QString path = item->data(0, PathRole).toString();

        // Open action
        auto* openAct = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                                       QStringLiteral("Open"), this, [this, path]() {
                                           if (!path.isEmpty()) {
                                               emit pathSelected(path);
                                           }
                                       });
        QFont f = openAct->font();
        f.setBold(true);
        openAct->setFont(f);

        // Open in New Tab
        if (!path.isEmpty()) {
            menu.addAction(QStringLiteral("Open in New Tab"), this,
                           [this, path]() { emit openInNewTabRequested(path); });
            menu.addAction(QStringLiteral("Open in New Window"), this, [path]() {
                QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                        {QStringLiteral("--new-window"), path});
            });
        }

        // Open in Terminal
        if (!isTrash && !path.isEmpty()) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")),
                           QStringLiteral("Open in Terminal"), this,
                           [path]() { DesktopAppsManager::openTerminal(path); });
        }

        // Copy Path
        if (!path.isEmpty()) {
            menu.addAction(QStringLiteral("Copy Path"), this,
                           [path]() { QApplication::clipboard()->setText(path); });
        }

        menu.addSeparator();

        // Location specific or Favorite specific actions
        if (isLocation) {
            bool isMounted = item->data(0, IsMountedRole).isValid()
                                 ? item->data(0, IsMountedRole).toBool()
                                 : true;
            bool isUsb = item->data(0, IsUsbRole).toBool();
            QString dev = item->data(0, DeviceRole).toString();

            if (isTrash) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("trash-empty")),
                               QStringLiteral("Empty Trash"), this, &Sidebar::emptyTrashRequested);
                menu.addSeparator();
            } else if (!isMounted) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("media-mount")),
                               QStringLiteral("Mount"), this, [this, dev, path]() {
                                   QString toMount = !dev.isEmpty() ? dev : path;
                                   mountDeviceAsync(toMount);
                               });
                menu.addSeparator();
            } else if (path != QStringLiteral("/") && !path.isEmpty()) {
                // Eject / Unmount
                menu.addAction(QIcon::fromTheme(QStringLiteral("media-eject")),
                               QStringLiteral("Eject / Unmount"), this,
                               [this, path]() { ejectVolumeAsync(path); });
                if (isUsb) {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("media-removable")),
                                   QStringLiteral("Safely Remove Drive"), this,
                                   [this, dev, path]() {
                                       ejectVolumeAsync(path);
                                       if (!dev.isEmpty()) {
                                           QProcess::startDetached(QStringLiteral("udisksctl"),
                                                                   {QStringLiteral("power-off"),
                                                                    QStringLiteral("-b"), dev});
                                       }
                                   });
                }
                menu.addSeparator();
            }

            // Disk Management (Partition Manager / GNOME Disks / GParted)
            if (path != QStringLiteral("/")) {
                QString diskTool;
                if (!QStandardPaths::findExecutable(QStringLiteral("partitionmanager")).isEmpty()) {
                    diskTool = QStringLiteral("partitionmanager");
                } else if (!QStandardPaths::findExecutable(QStringLiteral("gnome-disks"))
                                .isEmpty()) {
                    diskTool = QStringLiteral("gnome-disks");
                } else if (!QStandardPaths::findExecutable(QStringLiteral("gparted")).isEmpty()) {
                    diskTool = QStringLiteral("gparted");
                }
                if (!diskTool.isEmpty()) {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("drive-harddisk")),
                                   QStringLiteral("Manage Partition / Format..."),
                                   [diskTool, dev]() {
                                       if (!dev.isEmpty()) {
                                           QProcess::startDetached(diskTool, {dev});
                                       } else {
                                           QProcess::startDetached(diskTool, {});
                                       }
                                   });
                    menu.addSeparator();
                }
            }

            menu.addAction(QStringLiteral("Hide from Sidebar"), this, [this, path]() {
                AppSettings::instance().hideLocation(path);
                refresh();
            });
        } else if (isCustom) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-remove")),
                           QStringLiteral("Unpin from Sidebar"), this, [this, path]() {
                               AppSettings::instance().removeFavorite(path);
                               refresh();
                           });
        } else if (isStandard) {
            menu.addAction(QStringLiteral("Hide from Sidebar"), this, [this, path]() {
                AppSettings::instance().hideFavorite(path);
                refresh();
            });
        }

        // Get Info
        if (!path.isEmpty()) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")),
                           QStringLiteral("Get Info"), this,
                           [this, path]() { emit getInfoRequested(path); });
        }

        // Arrangement actions for Favorites
        if (isCustom || isStandard) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("go-top")),
                           QStringLiteral("Move to Top"), this, [this, path]() {
                               AppSettings::instance().moveFavoriteToTop(path);
                               refresh();
                               selectPath(path);
                           });
            auto* arrangeMenu = menu.addMenu(
                QIcon::fromTheme(QStringLiteral("view-sort-ascending")), QStringLiteral("Arrange"));
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-top")),
                                   QStringLiteral("Move to Top"), this, [this, path]() {
                                       AppSettings::instance().moveFavoriteToTop(path);
                                       refresh();
                                       selectPath(path);
                                   });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-up")),
                                   QStringLiteral("Move Up"), this, [this, path]() {
                                       AppSettings::instance().moveFavoriteUp(path);
                                       refresh();
                                       selectPath(path);
                                   });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-down")),
                                   QStringLiteral("Move Down"), this, [this, path]() {
                                       AppSettings::instance().moveFavoriteDown(path);
                                       refresh();
                                       selectPath(path);
                                   });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-bottom")),
                                   QStringLiteral("Move to Bottom"), this, [this, path]() {
                                       AppSettings::instance().moveFavoriteToBottom(path);
                                       refresh();
                                       selectPath(path);
                                   });
        }

        // Arrangement actions for Locations
        if (isLocation) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("go-top")),
                           QStringLiteral("Move to Top"), this, [this, path]() {
                               AppSettings::instance().moveLocationToTop(path);
                               refresh();
                               selectPath(path);
                           });
            auto* arrangeMenu = menu.addMenu(
                QIcon::fromTheme(QStringLiteral("view-sort-ascending")), QStringLiteral("Arrange"));
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-top")),
                                   QStringLiteral("Move to Top"), this, [this, path]() {
                                       AppSettings::instance().moveLocationToTop(path);
                                       refresh();
                                       selectPath(path);
                                   });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-up")),
                                   QStringLiteral("Move Up"), this, [this, path]() {
                                       AppSettings::instance().moveLocationUp(path);
                                       refresh();
                                       selectPath(path);
                                   });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-down")),
                                   QStringLiteral("Move Down"), this, [this, path]() {
                                       AppSettings::instance().moveLocationDown(path);
                                       refresh();
                                       selectPath(path);
                                   });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-bottom")),
                                   QStringLiteral("Move to Bottom"), this, [this, path]() {
                                       AppSettings::instance().moveLocationToBottom(path);
                                       refresh();
                                       selectPath(path);
                                   });
        }

        menu.addSeparator();
        menu.addAction(QStringLiteral("Manage Sidebar..."), this,
                       &Sidebar::openManageSidebarDialog);

    } else {
        // 4. Right clicked on empty space
        menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")),
                       QStringLiteral("Pin Folder..."), this, [this]() {
                           QString dir = QFileDialog::getExistingDirectory(
                               this, QStringLiteral("Select Folder to Pin to Sidebar"),
                               QDir::homePath());
                           if (!dir.isEmpty()) {
                               AppSettings::instance().addFavorite(dir);
                               refresh();
                           }
                       });

        QStringList hiddenFavs = AppSettings::instance().hiddenFavorites();
        QStringList hiddenLocs = AppSettings::instance().hiddenLocations();

        if (!hiddenFavs.isEmpty() || !hiddenLocs.isEmpty()) {
            auto* unhideMenu = menu.addMenu(QStringLiteral("Unhide"));
            for (const auto& hf : hiddenFavs) {
                QFileInfo fi(hf);
                QString label = fi.fileName().isEmpty() ? hf : fi.fileName();
                unhideMenu->addAction(QStringLiteral("Favorite: %1").arg(label), this,
                                      [this, hf]() {
                                          AppSettings::instance().unhideFavorite(hf);
                                          refresh();
                                      });
            }
            for (const auto& hl : hiddenLocs) {
                QFileInfo fi(hl);
                QString label = fi.fileName().isEmpty() ? hl : fi.fileName();
                unhideMenu->addAction(QStringLiteral("Location: %1").arg(label), this,
                                      [this, hl]() {
                                          AppSettings::instance().unhideLocation(hl);
                                          refresh();
                                      });
            }
        }

        menu.addSeparator();
        menu.addAction(QStringLiteral("Manage Sidebar..."), this,
                       &Sidebar::openManageSidebarDialog);
    }

    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void Sidebar::openManageSidebarDialog() {
    auto* dlg = new SidebarSettingsDialog(this);
    connect(dlg, &SidebarSettingsDialog::sidebarChanged, this, &Sidebar::refresh);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void Sidebar::startDrag(Qt::DropActions /*supportedActions*/) {
    QTreeWidgetItem* item = currentItem();
    if (!item)
        return;

    bool isFav = (item->parent() == m_favoritesHeader);
    bool isLoc = (item->parent() == m_locationsHeader);
    if (!isFav && !isLoc) {
        return; // Only allow dragging favorite items or location items
    }

    QString path = item->data(0, PathRole).toString();
    if (path.isEmpty())
        return;

    auto* mimeData = new QMimeData();
    if (isFav) {
        mimeData->setData(QStringLiteral("application/x-finder-sidebar-item"), path.toUtf8());
    } else {
        mimeData->setData(QStringLiteral("application/x-finder-sidebar-location"), path.toUtf8());
    }
    mimeData->setUrls({QUrl::fromLocalFile(path)});

    auto* drag = new QDrag(this);
    drag->setMimeData(mimeData);

    int textW = fontMetrics().horizontalAdvance(item->text(0));
    int width = qMin(220, qMax(140, textW + 50));
    QPixmap pixmap(width, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(40, 40, 45, 230));
    painter.setPen(QPen(QColor(100, 100, 110), 1));
    painter.drawRoundedRect(pixmap.rect().adjusted(1, 1, -1, -1), 6, 6);
    item->icon(0).paint(&painter, 8, 5, 18, 18);
    painter.setPen(Qt::white);
    painter.setFont(font());
    painter.drawText(QRect(32, 0, width - 36, 28), Qt::AlignVCenter | Qt::AlignLeft, item->text(0));
    painter.end();

    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(16, 14));

    drag->exec(Qt::MoveAction);
}

void Sidebar::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-item")) ||
        event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-location")) ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Sidebar::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-item"))) {
        QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
        QModelIndex idx = indexAt(vpPos);
        QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;
        if (item && (item == m_favoritesHeader || item->parent() == m_favoritesHeader)) {
            setCurrentItem(item);
            event->acceptProposedAction();
            return;
        }
        event->ignore();
        return;
    }

    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-location"))) {
        QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
        QModelIndex idx = indexAt(vpPos);
        QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;
        if (item && (item == m_locationsHeader || item->parent() == m_locationsHeader)) {
            setCurrentItem(item);
            event->acceptProposedAction();
            return;
        }
        event->ignore();
        return;
    }

    if (event->mimeData()->hasUrls()) {
        QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
        QModelIndex idx = indexAt(vpPos);
        QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;
        if (item) {
            setCurrentItem(item);
            if (AppSettings::instance().springLoadedFoldersEnabled()) {
                QString path = item->data(0, PathRole).toString();
                if (!path.isEmpty()) {
                    bool safe = true;
                    for (const auto& url : event->mimeData()->urls()) {
                        if (url.isLocalFile()) {
                            QString src = url.toLocalFile();
                            if (path == src || path.startsWith(src + QLatin1Char('/'))) {
                                safe = false;
                                break;
                            }
                        }
                    }
                    if (safe) {
                        if (m_springHoverPath != path) {
                            m_springHoverPath = path;
                            m_delegate->setSpringHoverPath(path);
                            viewport()->update();
                            m_springTimer->start(AppSettings::instance().springLoadDelayMs());
                        }
                    } else {
                        resetSpringTimer();
                    }
                } else {
                    resetSpringTimer();
                }
            }
        } else {
            clearSelection();
            resetSpringTimer();
        }
        event->acceptProposedAction();
    }
}

void Sidebar::dragLeaveEvent(QDragLeaveEvent* event) {
    resetSpringTimer();
    QTreeWidget::dragLeaveEvent(event);
}

void Sidebar::dropEvent(QDropEvent* event) {
    resetSpringTimer();
    // 1. Reordering favorite items inside sidebar
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-item"))) {
        QString draggedPath = QString::fromUtf8(
            event->mimeData()->data(QStringLiteral("application/x-finder-sidebar-item")));
        if (!draggedPath.isEmpty()) {
            QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
            QModelIndex idx = indexAt(vpPos);
            QTreeWidgetItem* target = idx.isValid() ? itemFromIndex(idx) : nullptr;

            if (target && (target == m_favoritesHeader || target->parent() == m_favoritesHeader)) {
                QStringList currentOrder = AppSettings::instance().favoriteOrder();
                int targetIndex = 0;
                if (target == m_favoritesHeader) {
                    targetIndex = 0; // Dropped on header -> move to top
                } else {
                    QString targetPath = target->data(0, PathRole).toString();
                    targetIndex = static_cast<int>(currentOrder.indexOf(targetPath));
                    if (targetIndex < 0)
                        targetIndex = 0;
                    QRect rect = visualItemRect(target);
                    if (vpPos.y() > rect.center().y()) {
                        targetIndex++; // Dropped on lower half -> place after target
                    }
                }

                AppSettings::instance().reorderFavorite(draggedPath, targetIndex);
                refresh();
                selectPath(draggedPath);
                event->acceptProposedAction();
                return;
            }
        }
        event->ignore();
        return;
    }

    // 2. Reordering location items inside sidebar
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-location"))) {
        QString draggedPath = QString::fromUtf8(
            event->mimeData()->data(QStringLiteral("application/x-finder-sidebar-location")));
        if (!draggedPath.isEmpty()) {
            QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
            QModelIndex idx = indexAt(vpPos);
            QTreeWidgetItem* target = idx.isValid() ? itemFromIndex(idx) : nullptr;

            if (target && (target == m_locationsHeader || target->parent() == m_locationsHeader)) {
                QStringList currentOrder = AppSettings::instance().locationOrder();
                int targetIndex = 0;
                if (target == m_locationsHeader) {
                    targetIndex = 0; // Dropped on header -> move to top
                } else {
                    QString targetPath = target->data(0, PathRole).toString();
                    targetIndex = static_cast<int>(currentOrder.indexOf(targetPath));
                    if (targetIndex < 0)
                        targetIndex = 0;
                    QRect rect = visualItemRect(target);
                    if (vpPos.y() > rect.center().y()) {
                        targetIndex++; // Dropped on lower half -> place after target
                    }
                }

                AppSettings::instance().reorderLocation(draggedPath, targetIndex);
                refresh();
                selectPath(draggedPath);
                event->acceptProposedAction();
                return;
            }
        }
        event->ignore();
        return;
    }

    // 2. External files dropped
    if (!event->mimeData()->hasUrls())
        return;

    QStringList sourcePaths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            sourcePaths.append(url.toLocalFile());
        }
    }
    if (sourcePaths.isEmpty())
        return;

    QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
    QModelIndex idx = indexAt(vpPos);
    QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;

    if (item) {
        if (item->data(0, IsTagRole).toBool()) {
            QString tag = item->data(0, TagNameRole).toString();
            for (const auto& sp : sourcePaths) {
                TagManager::instance().addTag(sp, tag);
            }
            refresh();
            event->acceptProposedAction();
            return;
        }

        bool isTrash = item->data(0, IsTrashRole).toBool();
        QString targetPath = item->data(0, PathRole).toString();

        if (isTrash) {
            emit itemsDroppedOnTrash(sourcePaths);
            event->acceptProposedAction();
            return;
        }

        if (!targetPath.isEmpty() && QFileInfo(targetPath).isDir()) {
            emit itemsDroppedOnPath(sourcePaths, targetPath, event->dropAction());
            event->acceptProposedAction();
            return;
        }

        int headerType = item->data(0, HeaderTypeRole).toInt();
        if (headerType == 1) {
            for (const auto& sp : sourcePaths) {
                if (QFileInfo(sp).isDir()) {
                    AppSettings::instance().addFavorite(sp);
                }
            }
            refresh();
            event->acceptProposedAction();
            return;
        }
    } else {
        // Dropped on empty space in sidebar -> pin folders to Favorites
        for (const auto& sp : sourcePaths) {
            if (QFileInfo(sp).isDir()) {
                AppSettings::instance().addFavorite(sp);
            }
        }
        refresh();
        event->acceptProposedAction();
    }
}

void Sidebar::resetSpringTimer() {
    if (m_springTimer && m_springTimer->isActive()) {
        m_springTimer->stop();
    }
    if (!m_springHoverPath.isEmpty()) {
        m_springHoverPath.clear();
        m_delegate->setSpringHoverPath(QString());
        viewport()->update();
    }
}

void Sidebar::onUdisksInterfacesAdded(const QDBusObjectPath& /*path*/,
                                      const QVariantMap& /*interfaces*/) {
    m_volumeScanTimestamp.invalidate();
    if (m_volumeDebounceTimer) {
        m_volumeDebounceTimer->start();
    }
}

void Sidebar::onUdisksInterfacesRemoved(const QDBusObjectPath& /*path*/,
                                        const QStringList& /*interfaces*/) {
    m_volumeScanTimestamp.invalidate();
    if (m_volumeDebounceTimer) {
        m_volumeDebounceTimer->start();
    }
}

void Sidebar::onSpringTimeout() {
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        selectPath(target);
        emit pathSelected(target);
    }
}

void Sidebar::checkVolumeChanges() {
    QStringList blockEntries =
        QDir(QStringLiteral("/sys/class/block")).entryList(QDir::NoDotAndDotDot);
    std::sort(blockEntries.begin(), blockEntries.end());

    QFileInfo mountsInfo(QStringLiteral("/proc/mounts"));
    qint64 mountsSize = mountsInfo.size();

    QString userMedia = QStringLiteral("/run/media/") + QString::fromUtf8(qgetenv("USER"));
    QStringList mediaEntries;
    if (QDir(userMedia).exists()) {
        mediaEntries = QDir(userMedia).entryList(QDir::NoDotAndDotDot);
    }

    QString fp = blockEntries.join(QLatin1Char(',')) + QLatin1Char('|') +
                 QString::number(mountsSize) + QLatin1Char('|') +
                 mediaEntries.join(QLatin1Char(','));

    if (m_lastVolumeFingerprint.isEmpty()) {
        m_lastVolumeFingerprint = fp;
        return;
    }

    if (fp != m_lastVolumeFingerprint) {
        m_lastVolumeFingerprint = fp;
        if (m_volumeWatcher && QDir(userMedia).exists() &&
            !m_volumeWatcher->directories().contains(userMedia)) {
            m_volumeWatcher->addPath(userMedia);
        }
        m_volumeScanTimestamp.invalidate();
        refresh();
    }
}
