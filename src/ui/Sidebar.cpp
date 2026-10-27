#include "Sidebar.h"
#include "AppSettings.h"
#include "XdgTrash.h"
#include <QStandardPaths>
#include <QDir>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

static constexpr int PathRole = Qt::UserRole + 10;
static constexpr int IsCustomFavRole = Qt::UserRole + 11;
static constexpr int IsTrashRole = Qt::UserRole + 12;

Sidebar::Sidebar(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(12);
    setAnimated(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    connect(this, &QTreeWidget::itemClicked, this, &Sidebar::onItemClicked);

    refresh();
}

void Sidebar::refresh()
{
    clear();

    populateFavorites();
    populateLocations();

    expandAll();
}

void Sidebar::populateFavorites()
{
    m_favoritesHeader = new QTreeWidgetItem(this);
    m_favoritesHeader->setText(0, QStringLiteral("Favorites"));
    QFont hf = m_favoritesHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(10);
    m_favoritesHeader->setFont(0, hf);
    m_favoritesHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_favoritesHeader->setFlags(Qt::ItemIsEnabled);

    QFileIconProvider iconProv;

    auto addFavItem = [this, &iconProv](const QString& name, const QString& path, const QString& iconThemeName, bool isCustom = false) {
        if (!QDir(path).exists()) return;

        auto* item = new QTreeWidgetItem(m_favoritesHeader);
        item->setText(0, name);
        item->setData(0, PathRole, path);
        item->setData(0, IsCustomFavRole, isCustom);

        QIcon icon = QIcon::fromTheme(iconThemeName, iconProv.icon(QFileIconProvider::Folder));
        item->setIcon(0, icon);
    };

    QString homePath = QDir::homePath();
    addFavItem(QStringLiteral("Home"), homePath, QStringLiteral("user-home"));

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    addFavItem(QStringLiteral("Desktop"), desktopPath, QStringLiteral("user-desktop"));

    QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    addFavItem(QStringLiteral("Documents"), docsPath, QStringLiteral("folder-documents"));

    QString downPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    addFavItem(QStringLiteral("Downloads"), downPath, QStringLiteral("folder-download"));

    QString picsPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    addFavItem(QStringLiteral("Pictures"), picsPath, QStringLiteral("folder-pictures"));

    QString musicPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    addFavItem(QStringLiteral("Music"), musicPath, QStringLiteral("folder-music"));

    QString videosPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    addFavItem(QStringLiteral("Videos"), videosPath, QStringLiteral("folder-videos"));

    // Custom favorites
    const QStringList customFavs = AppSettings::instance().customFavorites();
    for (const auto& fav : customFavs) {
        QFileInfo fi(fav);
        addFavItem(fi.fileName(), fav, QStringLiteral("folder"), true);
    }
}

void Sidebar::populateLocations()
{
    m_locationsHeader = new QTreeWidgetItem(this);
    m_locationsHeader->setText(0, QStringLiteral("Locations"));
    QFont hf = m_locationsHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(10);
    m_locationsHeader->setFont(0, hf);
    m_locationsHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_locationsHeader->setFlags(Qt::ItemIsEnabled);

    // Mounted volumes
    QVector<VolumeEntry> volumes = VolumeManager::mountedVolumes();
    for (const auto& vol : volumes) {
        auto* item = new QTreeWidgetItem(m_locationsHeader);
        item->setText(0, vol.name);
        item->setData(0, PathRole, vol.path);
        item->setIcon(0, vol.icon);
    }

    // Trash item
    auto* trashItem = new QTreeWidgetItem(m_locationsHeader);
    trashItem->setText(0, QStringLiteral("Trash"));
    trashItem->setData(0, PathRole, XdgTrash::trashFilesDirectory());
    trashItem->setData(0, IsTrashRole, true);
    trashItem->setIcon(0, QIcon::fromTheme(QStringLiteral("user-trash"),
                         QIcon::fromTheme(QStringLiteral("edit-delete"))));
}

void Sidebar::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    QString path = item->data(0, PathRole).toString();
    if (!path.isEmpty()) {
        emit pathSelected(path);
    }
}

void Sidebar::selectPath(const QString& path)
{
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

void Sidebar::contextMenuEvent(QContextMenuEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (!item) return;

    QMenu menu(this);
    bool isCustom = item->data(0, IsCustomFavRole).toBool();
    bool isTrash = item->data(0, IsTrashRole).toBool();
    QString path = item->data(0, PathRole).toString();

    if (isCustom) {
        auto* actRemove = menu.addAction(QStringLiteral("Remove from Sidebar"));
        connect(actRemove, &QAction::triggered, this, [this, path]() {
            AppSettings::instance().removeFavorite(path);
            refresh();
        });
    }

    if (isTrash) {
        auto* actEmpty = menu.addAction(QStringLiteral("Empty Trash"));
        connect(actEmpty, &QAction::triggered, this, &Sidebar::emptyTrashRequested);
    }

    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void Sidebar::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Sidebar::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

void Sidebar::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        for (const auto& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QString localPath = url.toLocalFile();
                if (QFileInfo(localPath).isDir()) {
                    AppSettings::instance().addFavorite(localPath);
                }
            }
        }
        refresh();
        event->acceptProposedAction();
    }
}
