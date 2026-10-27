#include "SidebarSettingsDialog.h"
#include "AppSettings.h"
#include "VolumeManager.h"
#include "XdgTrash.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFileDialog>
#include <QLabel>
#include <QFileIconProvider>
#include <QStandardPaths>
#include <QFileInfo>

SidebarSettingsDialog::SidebarSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Sidebar Preferences"));
    resize(460, 520);
    setupUi();
}

void SidebarSettingsDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(14);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget(scrollArea);
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(14);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // 1. Favorites Group
    auto* favGroup = new QGroupBox(QStringLiteral("Favorites"), scrollContent);
    auto* favLayout = new QVBoxLayout(favGroup);
    favLayout->setSpacing(8);

    m_favsContainer = new QWidget(favGroup);
    auto* favsInnerLayout = new QVBoxLayout(m_favsContainer);
    favsInnerLayout->setContentsMargins(0, 0, 0, 0);
    favsInnerLayout->setSpacing(6);

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

    QFileIconProvider iconProv;
    for (const auto& fav : defFavs) {
        if (!QDir(fav.path).exists()) continue;

        auto* cb = new QCheckBox(fav.name, m_favsContainer);
        QIcon icon = QIcon::fromTheme(fav.iconName,
                     QIcon::fromTheme(QStringLiteral("folder"),
                     iconProv.icon(QFileIconProvider::Folder)));
        cb->setIcon(icon);
        cb->setChecked(!AppSettings::instance().isFavoriteHidden(fav.path));

        connect(cb, &QCheckBox::toggled, this, [this, path = fav.path](bool checked) {
            if (checked) {
                AppSettings::instance().unhideFavorite(path);
            } else {
                AppSettings::instance().hideFavorite(path);
            }
            emit sidebarChanged();
        });

        m_standardFavBoxes.append({fav.path, cb});
        favsInnerLayout->addWidget(cb);
    }

    favLayout->addWidget(m_favsContainer);

    // Custom pinned folders
    auto* customLabel = new QLabel(QStringLiteral("<b>Pinned Folders:</b>"), favGroup);
    favLayout->addWidget(customLabel);

    m_customFavsList = new QListWidget(favGroup);
    m_customFavsList->setMaximumHeight(130);
    favLayout->addWidget(m_customFavsList);

    auto* pinBtnRow = new QHBoxLayout();
    auto* addPinBtn = new QPushButton(QStringLiteral("+ Pin Folder..."), favGroup);
    connect(addPinBtn, &QPushButton::clicked, this, &SidebarSettingsDialog::onPinFolderClicked);
    pinBtnRow->addWidget(addPinBtn);
    pinBtnRow->addStretch();
    favLayout->addLayout(pinBtnRow);

    contentLayout->addWidget(favGroup);

    // 2. Locations / Partitions Group
    auto* locGroup = new QGroupBox(QStringLiteral("Locations & Partitions"), scrollContent);
    auto* locLayout = new QVBoxLayout(locGroup);
    locLayout->setSpacing(6);

    m_locationsContainer = new QWidget(locGroup);
    auto* locsInnerLayout = new QVBoxLayout(m_locationsContainer);
    locsInnerLayout->setContentsMargins(0, 0, 0, 0);
    locsInnerLayout->setSpacing(6);

    auto volumes = VolumeManager::mountedVolumes();
    for (const auto& vol : volumes) {
        auto* cb = new QCheckBox(vol.name, m_locationsContainer);
        cb->setIcon(vol.icon);
        cb->setToolTip(vol.path);
        bool isHidden = AppSettings::instance().isLocationHidden(vol.path);
        cb->setChecked(!isHidden);

        connect(cb, &QCheckBox::toggled, this, [this, path = vol.path](bool checked) {
            if (checked) {
                AppSettings::instance().unhideLocation(path);
            } else {
                AppSettings::instance().hideLocation(path);
            }
            emit sidebarChanged();
        });

        m_locationBoxes.append({vol.path, cb});
        locsInnerLayout->addWidget(cb);
    }

    // Trash checkbox
    QString trashPath = XdgTrash::trashFilesDirectory();
    auto* trashCb = new QCheckBox(QStringLiteral("Trash"), m_locationsContainer);
    trashCb->setIcon(QIcon::fromTheme(QStringLiteral("user-trash"),
                     QIcon::fromTheme(QStringLiteral("trash-empty"),
                     iconProv.icon(QFileIconProvider::Trashcan))));
    trashCb->setChecked(!AppSettings::instance().isLocationHidden(trashPath) && !AppSettings::instance().isLocationHidden(QStringLiteral("Trash")));
    connect(trashCb, &QCheckBox::toggled, this, [this, trashPath](bool checked) {
        if (checked) {
            AppSettings::instance().unhideLocation(trashPath);
            AppSettings::instance().unhideLocation(QStringLiteral("Trash"));
        } else {
            AppSettings::instance().hideLocation(trashPath);
            AppSettings::instance().hideLocation(QStringLiteral("Trash"));
        }
        emit sidebarChanged();
    });
    m_locationBoxes.append({trashPath, trashCb});
    locsInnerLayout->addWidget(trashCb);

    locLayout->addWidget(m_locationsContainer);
    contentLayout->addWidget(locGroup);

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    // Bottom buttons
    auto* btnRow = new QHBoxLayout();
    auto* defaultsBtn = new QPushButton(QStringLiteral("Restore Defaults"), this);
    connect(defaultsBtn, &QPushButton::clicked, this, &SidebarSettingsDialog::onRestoreDefaultsClicked);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnRow->addWidget(defaultsBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    mainLayout->addLayout(btnRow);

    populateFavoritesList();
}

void SidebarSettingsDialog::populateFavoritesList()
{
    m_customFavsList->clear();
    const QStringList favs = AppSettings::instance().customFavorites();
    QFileIconProvider iconProv;

    for (const auto& fav : favs) {
        QFileInfo fi(fav);
        auto* item = new QListWidgetItem();
        item->setText(fi.fileName().isEmpty() ? fav : fi.fileName());
        item->setToolTip(fav);
        item->setIcon(QIcon::fromTheme(QStringLiteral("folder"), iconProv.icon(QFileIconProvider::Folder)));

        auto* rowWidget = new QWidget(m_customFavsList);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(4, 2, 4, 2);
        rowLayout->setSpacing(6);

        auto* nameLabel = new QLabel(QStringLiteral("<b>%1</b> (%2)").arg(fi.fileName(), fav), rowWidget);
        nameLabel->setToolTip(fav);
        rowLayout->addWidget(nameLabel);
        rowLayout->addStretch();

        auto* removeBtn = new QPushButton(QStringLiteral("Remove"), rowWidget);
        removeBtn->setFixedWidth(64);
        connect(removeBtn, &QPushButton::clicked, this, [this, fav]() {
            AppSettings::instance().removeFavorite(fav);
            populateFavoritesList();
            emit sidebarChanged();
        });
        rowLayout->addWidget(removeBtn);

        item->setSizeHint(rowWidget->sizeHint());
        m_customFavsList->addItem(item);
        m_customFavsList->setItemWidget(item, rowWidget);
    }
}

void SidebarSettingsDialog::onPinFolderClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Folder to Pin to Sidebar"), QDir::homePath());
    if (!dir.isEmpty()) {
        AppSettings::instance().addFavorite(dir);
        populateFavoritesList();
        emit sidebarChanged();
    }
}

void SidebarSettingsDialog::onRestoreDefaultsClicked()
{
    AppSettings::instance().resetSidebarDefaults();

    // Check all standard checkboxes
    for (auto& pair : m_standardFavBoxes) {
        pair.second->blockSignals(true);
        pair.second->setChecked(true);
        pair.second->blockSignals(false);
    }

    for (auto& pair : m_locationBoxes) {
        pair.second->blockSignals(true);
        pair.second->setChecked(true);
        pair.second->blockSignals(false);
    }

    populateFavoritesList();
    emit sidebarChanged();
}
