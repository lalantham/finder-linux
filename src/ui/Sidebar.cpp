#include "Sidebar.h"
#include "SidebarSettingsDialog.h"
#include "AppSettings.h"
#include "XdgTrash.h"
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>

static constexpr int PathRole = Qt::UserRole + 10;
static constexpr int IsCustomFavRole = Qt::UserRole + 11;
static constexpr int IsTrashRole = Qt::UserRole + 12;
static constexpr int IsLocationRole = Qt::UserRole + 13;
static constexpr int IsStandardFavRole = Qt::UserRole + 14;
static constexpr int HeaderTypeRole = Qt::UserRole + 15; // 1 = Favorites, 2 = Locations

Sidebar::Sidebar(QWidget* parent)
    : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(12);
    setAnimated(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    connect(this, &QTreeWidget::itemClicked, this, &Sidebar::onItemClicked);

    refresh();
}

void Sidebar::refresh()
{
    QString currentSelected;
    QTreeWidgetItem* curr = currentItem();
    if (curr) {
        currentSelected = curr->data(0, PathRole).toString();
    }

    clear();

    populateFavorites();
    populateLocations();

    expandAll();

    if (!currentSelected.isEmpty()) {
        selectPath(currentSelected);
    }
}

void Sidebar::populateFavorites()
{
    m_favoritesHeader = new QTreeWidgetItem(this);
    m_favoritesHeader->setText(0, QStringLiteral("Favorites"));
    m_favoritesHeader->setData(0, HeaderTypeRole, 1);
    QFont hf = m_favoritesHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(10);
    m_favoritesHeader->setFont(0, hf);
    m_favoritesHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_favoritesHeader->setFlags(Qt::ItemIsEnabled);

    auto addFavItem = [this](const QString& name, const QString& path, const QString& themeIconName, bool isCustom) {
        if (!QDir(path).exists()) return;

        auto* item = new QTreeWidgetItem(m_favoritesHeader);
        item->setText(0, name);
        item->setData(0, PathRole, path);
        item->setData(0, IsCustomFavRole, isCustom);
        item->setData(0, IsStandardFavRole, !isCustom);

        QFileIconProvider iconProv;
        QIcon icon = QIcon::fromTheme(themeIconName,
                     QIcon::fromTheme(QStringLiteral("folder"),
                     iconProv.icon(QFileIconProvider::Folder)));
        item->setIcon(0, icon);
    };

    struct StandardFav {
        QString name;
        QString path;
        QString iconName;
    };
    QVector<StandardFav> defFavs = {
        { QStringLiteral("Home"), QDir::homePath(), QStringLiteral("user-home") },
        { QStringLiteral("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), QStringLiteral("user-desktop") },
        { QStringLiteral("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QStringLiteral("folder-documents") },
        { QStringLiteral("Downloads"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), QStringLiteral("folder-download") },
        { QStringLiteral("Pictures"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), QStringLiteral("folder-pictures") },
        { QStringLiteral("Music"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QStringLiteral("folder-music") },
        { QStringLiteral("Videos"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), QStringLiteral("folder-videos") }
    };

    for (const auto& fav : defFavs) {
        if (!AppSettings::instance().isFavoriteHidden(fav.path)) {
            addFavItem(fav.name, fav.path, fav.iconName, false);
        }
    }

    // Custom pinned favorites
    const QStringList customFavs = AppSettings::instance().customFavorites();
    for (const auto& fav : customFavs) {
        if (!AppSettings::instance().isFavoriteHidden(fav)) {
            QFileInfo fi(fav);
            addFavItem(fi.fileName().isEmpty() ? fav : fi.fileName(), fav, QStringLiteral("folder"), true);
        }
    }
}

void Sidebar::populateLocations()
{
    m_locationsHeader = new QTreeWidgetItem(this);
    m_locationsHeader->setText(0, QStringLiteral("Locations"));
    m_locationsHeader->setData(0, HeaderTypeRole, 2);
    QFont hf = m_locationsHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(10);
    m_locationsHeader->setFont(0, hf);
    m_locationsHeader->setForeground(0, QBrush(QColor(128, 128, 128)));
    m_locationsHeader->setFlags(Qt::ItemIsEnabled);

    QFileIconProvider iconProv;

    // Mounted volumes
    QVector<VolumeEntry> volumes = VolumeManager::mountedVolumes();
    for (const auto& vol : volumes) {
        if (AppSettings::instance().isLocationHidden(vol.path)) {
            continue;
        }

        auto* item = new QTreeWidgetItem(m_locationsHeader);
        item->setText(0, vol.name);
        item->setData(0, PathRole, vol.path);
        item->setData(0, IsLocationRole, true);
        item->setIcon(0, vol.icon);
    }

    // Trash item
    QString trashPath = XdgTrash::trashFilesDirectory();
    if (!AppSettings::instance().isLocationHidden(trashPath) && !AppSettings::instance().isLocationHidden(QStringLiteral("Trash"))) {
        auto* trashItem = new QTreeWidgetItem(m_locationsHeader);
        trashItem->setText(0, QStringLiteral("Trash"));
        trashItem->setData(0, PathRole, trashPath);
        trashItem->setData(0, IsTrashRole, true);
        trashItem->setData(0, IsLocationRole, true);
        trashItem->setIcon(0, QIcon::fromTheme(QStringLiteral("user-trash"),
                             QIcon::fromTheme(QStringLiteral("trash-empty"),
                             QIcon::fromTheme(QStringLiteral("edit-delete"),
                             iconProv.icon(QFileIconProvider::Trashcan)))));
    }
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
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), QStringLiteral("Pin Folder..."), this, [this]() {
                QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Folder to Pin to Sidebar"), QDir::homePath());
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
            menu.addAction(QStringLiteral("Manage Sidebar..."), this, &Sidebar::openManageSidebarDialog);
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
            menu.addAction(QStringLiteral("Manage Sidebar..."), this, &Sidebar::openManageSidebarDialog);
            menu.exec(event->globalPos());
            return;
        }

        // 3. Right clicked specific item
        bool isCustom = item->data(0, IsCustomFavRole).toBool();
        bool isStandard = item->data(0, IsStandardFavRole).toBool();
        bool isTrash = item->data(0, IsTrashRole).toBool();
        bool isLocation = item->data(0, IsLocationRole).toBool();
        QString path = item->data(0, PathRole).toString();

        // Open action
        auto* openAct = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this, [this, path]() {
            if (!path.isEmpty()) {
                emit pathSelected(path);
            }
        });
        QFont f = openAct->font();
        f.setBold(true);
        openAct->setFont(f);

        // Open in New Tab
        if (!path.isEmpty()) {
            menu.addAction(QStringLiteral("Open in New Tab"), this, [this, path]() {
                emit openInNewTabRequested(path);
            });
        }

        // Open in Terminal
        if (!isTrash && !path.isEmpty()) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), QStringLiteral("Open in Terminal"), this, [path]() {
                QStringList terms = {
                    QStringLiteral("x-terminal-emulator"),
                    QStringLiteral("ptyxis"),
                    QStringLiteral("gnome-terminal"),
                    QStringLiteral("konsole"),
                    QStringLiteral("xfce4-terminal"),
                    QStringLiteral("kitty"),
                    QStringLiteral("alacritty"),
                    QStringLiteral("xterm")
                };
                for (const auto& term : terms) {
                    if (QProcess::startDetached(term, QStringList(), path)) {
                        break;
                    }
                }
            });
        }

        // Copy Path
        if (!path.isEmpty()) {
            menu.addAction(QStringLiteral("Copy Path"), this, [path]() {
                QApplication::clipboard()->setText(path);
            });
        }

        menu.addSeparator();

        // Location specific or Favorite specific actions
        if (isLocation) {
            if (isTrash) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("trash-empty")), QStringLiteral("Empty Trash"), this, &Sidebar::emptyTrashRequested);
                menu.addSeparator();
            } else if (path != QStringLiteral("/") && !path.isEmpty()) {
                // Eject / Unmount
                menu.addAction(QIcon::fromTheme(QStringLiteral("media-eject")), QStringLiteral("Eject / Unmount"), this, [this, path]() {
                    QString err;
                    if (VolumeManager::unmountVolume(path, &err)) {
                        emit volumeUnmounted(path);
                        refresh();
                    } else {
                        QMessageBox::warning(this, QStringLiteral("Cannot Unmount"), err);
                    }
                });
                menu.addSeparator();
            }

            menu.addAction(QStringLiteral("Hide from Sidebar"), this, [this, path]() {
                AppSettings::instance().hideLocation(path);
                refresh();
            });
        } else if (isCustom) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-remove")), QStringLiteral("Unpin from Sidebar"), this, [this, path]() {
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
            menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), QStringLiteral("Get Info"), this, [this, path]() {
                emit getInfoRequested(path);
            });
        }

        menu.addSeparator();
        menu.addAction(QStringLiteral("Manage Sidebar..."), this, &Sidebar::openManageSidebarDialog);

    } else {
        // 4. Right clicked on empty space
        menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), QStringLiteral("Pin Folder..."), this, [this]() {
            QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Folder to Pin to Sidebar"), QDir::homePath());
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
                unhideMenu->addAction(QStringLiteral("Favorite: %1").arg(label), this, [this, hf]() {
                    AppSettings::instance().unhideFavorite(hf);
                    refresh();
                });
            }
            for (const auto& hl : hiddenLocs) {
                QFileInfo fi(hl);
                QString label = fi.fileName().isEmpty() ? hl : fi.fileName();
                unhideMenu->addAction(QStringLiteral("Location: %1").arg(label), this, [this, hl]() {
                    AppSettings::instance().unhideLocation(hl);
                    refresh();
                });
            }
        }

        menu.addSeparator();
        menu.addAction(QStringLiteral("Manage Sidebar..."), this, &Sidebar::openManageSidebarDialog);
    }

    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void Sidebar::openManageSidebarDialog()
{
    auto* dlg = new SidebarSettingsDialog(this);
    connect(dlg, &SidebarSettingsDialog::sidebarChanged, this, &Sidebar::refresh);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void Sidebar::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Sidebar::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
        QModelIndex idx = indexAt(vpPos);
        QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;
        if (item) {
            setCurrentItem(item);
        } else {
            clearSelection();
        }
        event->acceptProposedAction();
    }
}

void Sidebar::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    QStringList sourcePaths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            sourcePaths.append(url.toLocalFile());
        }
    }
    if (sourcePaths.isEmpty()) return;

    QPoint vpPos = viewport()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
    QModelIndex idx = indexAt(vpPos);
    QTreeWidgetItem* item = idx.isValid() ? itemFromIndex(idx) : nullptr;

    if (item) {
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
