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
    auto* favGroup = new QGroupBox(QStringLiteral("Favorites (Reorder & Visibility)"), scrollContent);
    auto* favLayout = new QVBoxLayout(favGroup);
    favLayout->setSpacing(8);

    auto* favHint = new QLabel(QStringLiteral("Arrange favorites order using <b>Top</b>, <b>▲</b>, <b>▼</b>:"), favGroup);
    favHint->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    favLayout->addWidget(favHint);

    m_favsList = new QListWidget(favGroup);
    m_favsList->setMinimumHeight(240);
    favLayout->addWidget(m_favsList);

    auto* pinBtnRow = new QHBoxLayout();
    auto* addPinBtn = new QPushButton(QStringLiteral("+ Pin Folder..."), favGroup);
    connect(addPinBtn, &QPushButton::clicked, this, &SidebarSettingsDialog::onPinFolderClicked);
    pinBtnRow->addWidget(addPinBtn);

    auto* resetOrderBtn = new QPushButton(QStringLiteral("Reset Order"), favGroup);
    resetOrderBtn->setToolTip(QStringLiteral("Reset favorites order back to standard defaults"));
    connect(resetOrderBtn, &QPushButton::clicked, this, [this]() {
        AppSettings::instance().setFavoriteOrder({});
        populateFavoritesList();
        emit sidebarChanged();
    });
    pinBtnRow->addWidget(resetOrderBtn);

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

    QFileIconProvider iconProv;
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
    m_favsList->clear();
    const QStringList orderedFavs = AppSettings::instance().favoriteOrder();
    const QStringList customFavs = AppSettings::instance().customFavorites();
    const QString homePath = QDir::homePath();
    const QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString downPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString picsPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString musicPath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    const QString vidsPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);

    QFileIconProvider iconProv;

    for (int i = 0; i < orderedFavs.size(); ++i) {
        const QString& path = orderedFavs[i];
        if (!QDir(path).exists()) continue;

        bool isCustom = customFavs.contains(path);
        QString name;
        QString iconName = QStringLiteral("folder");

        if (path == homePath) {
            name = QStringLiteral("Home");
            iconName = QStringLiteral("user-home");
        } else if (path == desktopPath) {
            name = QStringLiteral("Desktop");
            iconName = QStringLiteral("user-desktop");
        } else if (path == docsPath) {
            name = QStringLiteral("Documents");
            iconName = QStringLiteral("folder-documents");
        } else if (path == downPath) {
            name = QStringLiteral("Downloads");
            iconName = QStringLiteral("folder-download");
        } else if (path == picsPath) {
            name = QStringLiteral("Pictures");
            iconName = QStringLiteral("folder-pictures");
        } else if (path == musicPath) {
            name = QStringLiteral("Music");
            iconName = QStringLiteral("folder-music");
        } else if (path == vidsPath) {
            name = QStringLiteral("Videos");
            iconName = QStringLiteral("folder-videos");
        } else {
            QFileInfo fi(path);
            name = fi.fileName().isEmpty() ? path : fi.fileName();
            isCustom = true;
        }

        auto* item = new QListWidgetItem();
        auto* rowWidget = new QWidget(m_favsList);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(6, 2, 6, 2);
        rowLayout->setSpacing(6);

        auto* cb = new QCheckBox(rowWidget);
        cb->setChecked(!AppSettings::instance().isFavoriteHidden(path));
        connect(cb, &QCheckBox::toggled, this, [this, path](bool checked) {
            if (checked) {
                AppSettings::instance().unhideFavorite(path);
            } else {
                AppSettings::instance().hideFavorite(path);
            }
            emit sidebarChanged();
        });
        rowLayout->addWidget(cb);

        auto* iconLabel = new QLabel(rowWidget);
        QIcon icon = QIcon::fromTheme(iconName,
                     QIcon::fromTheme(QStringLiteral("folder"),
                     iconProv.icon(QFileIconProvider::Folder)));
        iconLabel->setPixmap(icon.pixmap(18, 18));
        rowLayout->addWidget(iconLabel);

        auto* nameLabel = new QLabel(isCustom ? QStringLiteral("<b>%1</b> <span style='color:#888;'>(%2)</span>").arg(name, path) : QStringLiteral("<b>%1</b>").arg(name), rowWidget);
        rowLayout->addWidget(nameLabel);
        rowLayout->addStretch();

        // Top button
        auto* topBtn = new QPushButton(QStringLiteral("Top"), rowWidget);
        topBtn->setToolTip(QStringLiteral("Move to Top"));
        topBtn->setFixedWidth(38);
        topBtn->setEnabled(i > 0);
        connect(topBtn, &QPushButton::clicked, this, [this, path]() {
            AppSettings::instance().moveFavoriteToTop(path);
            populateFavoritesList();
            emit sidebarChanged();
        });
        rowLayout->addWidget(topBtn);

        // Up button
        auto* upBtn = new QPushButton(QStringLiteral("▲"), rowWidget);
        upBtn->setToolTip(QStringLiteral("Move Up"));
        upBtn->setFixedWidth(28);
        upBtn->setEnabled(i > 0);
        connect(upBtn, &QPushButton::clicked, this, [this, path]() {
            AppSettings::instance().moveFavoriteUp(path);
            populateFavoritesList();
            emit sidebarChanged();
        });
        rowLayout->addWidget(upBtn);

        // Down button
        auto* downBtn = new QPushButton(QStringLiteral("▼"), rowWidget);
        downBtn->setToolTip(QStringLiteral("Move Down"));
        downBtn->setFixedWidth(28);
        downBtn->setEnabled(i < orderedFavs.size() - 1);
        connect(downBtn, &QPushButton::clicked, this, [this, path]() {
            AppSettings::instance().moveFavoriteDown(path);
            populateFavoritesList();
            emit sidebarChanged();
        });
        rowLayout->addWidget(downBtn);

        // Remove button (if custom pinned)
        if (isCustom) {
            auto* removeBtn = new QPushButton(QStringLiteral("✕"), rowWidget);
            removeBtn->setToolTip(QStringLiteral("Unpin from Sidebar"));
            removeBtn->setFixedWidth(28);
            connect(removeBtn, &QPushButton::clicked, this, [this, path]() {
                AppSettings::instance().removeFavorite(path);
                populateFavoritesList();
                emit sidebarChanged();
            });
            rowLayout->addWidget(removeBtn);
        }

        item->setSizeHint(rowWidget->sizeHint());
        m_favsList->addItem(item);
        m_favsList->setItemWidget(item, rowWidget);
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

    for (auto& pair : m_locationBoxes) {
        pair.second->blockSignals(true);
        pair.second->setChecked(true);
        pair.second->blockSignals(false);
    }

    populateFavoritesList();
    emit sidebarChanged();
}
