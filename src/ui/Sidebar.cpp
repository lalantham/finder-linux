#include "Sidebar.h"
#include "SidebarSettingsDialog.h"
#include "AppSettings.h"
#include "XdgTrash.h"
#include "TagManager.h"
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
#include <QDrag>
#include <QPainter>

static constexpr int PathRole = Qt::UserRole + 10;
static constexpr int IsCustomFavRole = Qt::UserRole + 11;
static constexpr int IsTrashRole = Qt::UserRole + 12;
static constexpr int IsLocationRole = Qt::UserRole + 13;
static constexpr int IsStandardFavRole = Qt::UserRole + 14;
static constexpr int HeaderTypeRole = Qt::UserRole + 15; // 1 = Favorites, 2 = Locations, 3 = Tags
static constexpr int IsTagRole = Qt::UserRole + 16;
static constexpr int TagNameRole = Qt::UserRole + 17;

Sidebar::Sidebar(QWidget* parent)
    : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setIndentation(12);
    setAnimated(true);
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    connect(this, &QTreeWidget::itemClicked, this, &Sidebar::onItemClicked);
    connect(&TagManager::instance(), &TagManager::tagsChanged, this, &Sidebar::refresh);

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
    populateTags();

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

    const QStringList orderedFavs = AppSettings::instance().favoriteOrder();
    const QString homePath = QDir::homePath();
    const QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString downPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString picsPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString musicPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString vidsPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);

    for (const auto& path : orderedFavs) {
        if (!QDir(path).exists()) continue;
        if (AppSettings::instance().isFavoriteHidden(path)) continue;

        if (path == homePath) {
            addFavItem(QStringLiteral("Home"), path, QStringLiteral("user-home"), false);
        } else if (path == desktopPath) {
            addFavItem(QStringLiteral("Desktop"), path, QStringLiteral("user-desktop"), false);
        } else if (path == docsPath) {
            addFavItem(QStringLiteral("Documents"), path, QStringLiteral("folder-documents"), false);
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
            addFavItem(fi.fileName().isEmpty() ? path : fi.fileName(), path, QStringLiteral("folder"), true);
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

    QVector<VolumeEntry> volumes = VolumeManager::mountedVolumes();
    QString trashPath = XdgTrash::trashFilesDirectory();

    struct LocData {
        QString name;
        QIcon icon;
        bool isTrash{false};
    };
    QMap<QString, LocData> map;
    for (const auto& vol : volumes) {
        map[vol.path] = {vol.name, vol.icon, false};
    }
    map[trashPath] = {
        QStringLiteral("Trash"),
        QIcon::fromTheme(QStringLiteral("user-trash"),
        QIcon::fromTheme(QStringLiteral("trash-empty"),
        QIcon::fromTheme(QStringLiteral("edit-delete"),
        iconProv.icon(QFileIconProvider::Trashcan)))),
        true
    };

    const QStringList orderedLocs = AppSettings::instance().locationOrder();
    for (const auto& path : orderedLocs) {
        if (!map.contains(path)) continue;
        if (AppSettings::instance().isLocationHidden(path)) continue;
        if (path == trashPath && AppSettings::instance().isLocationHidden(QStringLiteral("Trash"))) continue;

        const auto& data = map[path];
        auto* item = new QTreeWidgetItem(m_locationsHeader);
        item->setText(0, data.name);
        item->setData(0, PathRole, path);
        item->setData(0, IsLocationRole, true);
        if (data.isTrash) {
            item->setData(0, IsTrashRole, true);
        }
        item->setIcon(0, data.icon);
    }
}

void Sidebar::populateTags()
{
    m_tagsHeader = new QTreeWidgetItem(this);
    m_tagsHeader->setText(0, QStringLiteral("Tags"));
    m_tagsHeader->setData(0, HeaderTypeRole, 3);
    QFont hf = m_tagsHeader->font(0);
    hf.setBold(true);
    hf.setPointSize(10);
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

void Sidebar::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    if (item->data(0, IsTagRole).toBool()) {
        QString tag = item->data(0, TagNameRole).toString();
        emit tagSelected(tag);
        return;
    }
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

        // 3. Right clicked on Tag item
        if (item->data(0, IsTagRole).toBool()) {
            QString tag = item->data(0, TagNameRole).toString();
            auto* openAct = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this, [this, tag]() {
                emit tagSelected(tag);
            });
            QFont f = openAct->font();
            f.setBold(true);
            openAct->setFont(f);

            menu.addSeparator();
            int count = TagManager::instance().tagCount(tag);
            auto* clearAct = menu.addAction(QStringLiteral("Clear All \"%1\" Tags (%2 Files)").arg(tag).arg(count), this, [this, tag]() {
                QStringList files = TagManager::instance().filesForTag(tag);
                for (const auto& f : files) {
                    TagManager::instance().removeTag(f, tag);
                }
                refresh();
            });
            if (count == 0) clearAct->setEnabled(false);

            menu.exec(event->globalPos());
            return;
        }

        // 4. Right clicked specific item
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

        // Arrangement actions for Favorites
        if (isCustom || isStandard) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("go-top")), QStringLiteral("Move to Top"), this, [this, path]() {
                AppSettings::instance().moveFavoriteToTop(path);
                refresh();
                selectPath(path);
            });
            auto* arrangeMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("view-sort-ascending")), QStringLiteral("Arrange"));
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-top")), QStringLiteral("Move to Top"), this, [this, path]() {
                AppSettings::instance().moveFavoriteToTop(path);
                refresh();
                selectPath(path);
            });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-up")), QStringLiteral("Move Up"), this, [this, path]() {
                AppSettings::instance().moveFavoriteUp(path);
                refresh();
                selectPath(path);
            });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-down")), QStringLiteral("Move Down"), this, [this, path]() {
                AppSettings::instance().moveFavoriteDown(path);
                refresh();
                selectPath(path);
            });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-bottom")), QStringLiteral("Move to Bottom"), this, [this, path]() {
                AppSettings::instance().moveFavoriteToBottom(path);
                refresh();
                selectPath(path);
            });
        }

        // Arrangement actions for Locations
        if (isLocation) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("go-top")), QStringLiteral("Move to Top"), this, [this, path]() {
                AppSettings::instance().moveLocationToTop(path);
                refresh();
                selectPath(path);
            });
            auto* arrangeMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("view-sort-ascending")), QStringLiteral("Arrange"));
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-top")), QStringLiteral("Move to Top"), this, [this, path]() {
                AppSettings::instance().moveLocationToTop(path);
                refresh();
                selectPath(path);
            });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-up")), QStringLiteral("Move Up"), this, [this, path]() {
                AppSettings::instance().moveLocationUp(path);
                refresh();
                selectPath(path);
            });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-down")), QStringLiteral("Move Down"), this, [this, path]() {
                AppSettings::instance().moveLocationDown(path);
                refresh();
                selectPath(path);
            });
            arrangeMenu->addAction(QIcon::fromTheme(QStringLiteral("go-bottom")), QStringLiteral("Move to Bottom"), this, [this, path]() {
                AppSettings::instance().moveLocationToBottom(path);
                refresh();
                selectPath(path);
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

void Sidebar::startDrag(Qt::DropActions /*supportedActions*/)
{
    QTreeWidgetItem* item = currentItem();
    if (!item) return;

    bool isFav = (item->parent() == m_favoritesHeader);
    bool isLoc = (item->parent() == m_locationsHeader);
    if (!isFav && !isLoc) {
        return; // Only allow dragging favorite items or location items
    }

    QString path = item->data(0, PathRole).toString();
    if (path.isEmpty()) return;

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

void Sidebar::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-item")) ||
        event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-location")) ||
        event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Sidebar::dragMoveEvent(QDragMoveEvent* event)
{
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
        } else {
            clearSelection();
        }
        event->acceptProposedAction();
    }
}

void Sidebar::dropEvent(QDropEvent* event)
{
    // 1. Reordering favorite items inside sidebar
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-finder-sidebar-item"))) {
        QString draggedPath = QString::fromUtf8(event->mimeData()->data(QStringLiteral("application/x-finder-sidebar-item")));
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
                    if (targetIndex < 0) targetIndex = 0;
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
        QString draggedPath = QString::fromUtf8(event->mimeData()->data(QStringLiteral("application/x-finder-sidebar-location")));
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
                    if (targetIndex < 0) targetIndex = 0;
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
