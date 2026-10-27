#include "GetInfoDialog.h"
#include "TagManager.h"
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileInfo>
#include <QDirIterator>
#include <QPushButton>
#include <QMessageBox>
#include <sys/stat.h>

FolderSizeWorker::FolderSizeWorker(QString folderPath)
    : m_folderPath(std::move(folderPath))
{
}

void FolderSizeWorker::cancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

void FolderSizeWorker::calculate()
{
    qint64 totalBytes = 0;
    int files = 0;
    int folders = 0;

    QDirIterator it(m_folderPath, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (m_cancelled.load(std::memory_order_relaxed)) return;
        it.next();
        QFileInfo fi = it.fileInfo();
        if (fi.isDir()) {
            folders++;
        } else {
            files++;
            totalBytes += fi.size();
        }
    }

    emit calculated(totalBytes, files, folders);
}

GetInfoDialog::GetInfoDialog(const QString& path, QWidget* parent)
    : QDialog(parent)
    , m_path(path)
    , m_item(path)
{
    setWindowTitle(QStringLiteral("%1 Info").arg(m_item.fileName()));
    setFixedWidth(420);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(14);
    mainLayout->setContentsMargins(18, 18, 18, 18);

    // Top Header: Icon + Name
    auto* header = new QHBoxLayout();
    header->setSpacing(14);

    auto* iconLabel = new QLabel(this);
    iconLabel->setPixmap(m_item.icon().pixmap(64, 64));
    header->addWidget(iconLabel);

    auto* nameLayout = new QVBoxLayout();
    auto* nameLabel = new QLabel(m_item.fileName(), this);
    QFont f = nameLabel->font();
    f.setBold(true);
    f.setPointSize(12);
    nameLabel->setFont(f);
    nameLabel->setWordWrap(true);

    auto* kindSub = new QLabel(m_item.kind(), this);
    kindSub->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(kindSub);
    header->addLayout(nameLayout);
    header->addStretch();
    mainLayout->addLayout(header);

    setupGeneralSection(mainLayout, m_item);
    setupTagsSection(mainLayout, m_path);

    if (!m_item.isDir()) {
        setupOpenWithSection(mainLayout, m_item);
    }

    setupPermissionsSection(mainLayout, m_item);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);

    // Calculate folder size asynchronously if it's a directory
    if (m_item.isDir()) {
        m_workerThread = new QThread(this);
        m_worker = new FolderSizeWorker(m_path);
        m_worker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::started, m_worker, &FolderSizeWorker::calculate);
        connect(m_worker, &FolderSizeWorker::calculated, this, &GetInfoDialog::onFolderSizeCalculated);
        connect(m_worker, &FolderSizeWorker::calculated, m_workerThread, &QThread::quit);

        m_workerThread->start();
    }
}

GetInfoDialog::~GetInfoDialog()
{
    if (m_worker) {
        m_worker->cancel();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(300);
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
            m_workerThread->wait();
        }
    }
}

void GetInfoDialog::setupGeneralSection(QVBoxLayout* layout, const FileItem& item)
{
    auto* generalBox = new QGroupBox(QStringLiteral("General:"), this);
    auto* generalForm = new QFormLayout(generalBox);
    generalForm->setLabelAlignment(Qt::AlignRight);

    generalForm->addRow(QStringLiteral("Kind:"), new QLabel(item.kind(), generalBox));

    m_sizeValueLabel = new QLabel(item.isDir() ? QStringLiteral("Calculating size...") : item.formattedSize(), generalBox);
    generalForm->addRow(QStringLiteral("Size:"), m_sizeValueLabel);

    if (!item.isDir()) {
        generalForm->addRow(QStringLiteral("Disk Usage:"), new QLabel(FileItem::formatSize(item.diskUsage()), generalBox));
    }

    auto* whereLabel = new QLabel(item.path(), generalBox);
    whereLabel->setWordWrap(true);
    generalForm->addRow(QStringLiteral("Where:"), whereLabel);

    generalForm->addRow(QStringLiteral("Created:"), new QLabel(item.created().toString(QStringLiteral("yyyy-MM-dd hh:mm")), generalBox));
    generalForm->addRow(QStringLiteral("Modified:"), new QLabel(item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm")), generalBox));

    if (item.isSymLink()) {
        generalForm->addRow(QStringLiteral("Original:"), new QLabel(item.symLinkTarget(), generalBox));
    }

    layout->addWidget(generalBox);
}

void GetInfoDialog::setupTagsSection(QVBoxLayout* layout, const QString& path)
{
    auto* tagBox = new QGroupBox(QStringLiteral("Tags:"), this);
    auto* tagLayout = new QHBoxLayout(tagBox);
    tagLayout->setSpacing(8);
    tagLayout->setContentsMargins(12, 10, 12, 10);

    for (const auto& tagInfo : TagManager::standardTags()) {
        auto* btn = new QToolButton(tagBox);
        btn->setFixedSize(24, 24);
        btn->setCheckable(true);
        btn->setChecked(TagManager::instance().hasTag(path, tagInfo.name));
        btn->setToolTip(tagInfo.name);

        auto updateStyle = [btn, color = tagInfo.color]() {
            bool checked = btn->isChecked();
            btn->setStyleSheet(QStringLiteral(
                "QToolButton {"
                "  border-radius: 12px;"
                "  background-color: %1;"
                "  border: %2;"
                "}"
            ).arg(color.name()).arg(checked ? QStringLiteral("3px solid #ffffff") : QStringLiteral("1px solid rgba(0,0,0,0.25)")));
        };
        updateStyle();

        connect(btn, &QToolButton::clicked, this, [path, tag = tagInfo.name, btn, updateStyle]() {
            TagManager::instance().toggleTag(path, tag);
            updateStyle();
        });

        tagLayout->addWidget(btn);
    }
    tagLayout->addStretch();
    layout->addWidget(tagBox);
}

void GetInfoDialog::setupOpenWithSection(QVBoxLayout* layout, const FileItem& item)
{
    auto* openWithBox = new QGroupBox(QStringLiteral("Open with:"), this);
    auto* boxLayout = new QVBoxLayout(openWithBox);
    boxLayout->setSpacing(8);

    m_appCombo = new QComboBox(openWithBox);
    m_appCombo->setIconSize(QSize(20, 20));

    QVector<AppEntry> apps = DesktopAppsManager::appsForMime(item.mimeTypeName());
    int defaultIndex = 0;
    for (int i = 0; i < apps.size(); ++i) {
        const auto& app = apps.at(i);
        QString label = app.name;
        if (app.isDefault) {
            label += QStringLiteral(" (default)");
            defaultIndex = i;
        }
        m_appCombo->addItem(app.icon, label, app.id);
    }

    if (apps.isEmpty()) {
        m_appCombo->addItem(QStringLiteral("System Default Application"), QString());
    } else {
        m_appCombo->setCurrentIndex(defaultIndex);
    }
    boxLayout->addWidget(m_appCombo);

    auto* descLabel = new QLabel(
        QStringLiteral("Use this application to open all documents of this type (%1).")
            .arg(item.suffix().isEmpty() ? item.kind() : QStringLiteral(".%1").arg(item.suffix())), openWithBox);
    descLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    descLabel->setWordWrap(true);
    boxLayout->addWidget(descLabel);

    auto* btnRow = new QHBoxLayout();
    m_changeAllBtn = new QPushButton(QStringLiteral("Change All..."), openWithBox);
    m_changeAllBtn->setToolTip(QStringLiteral("Set this application as default for all files of this type"));
    connect(m_changeAllBtn, &QPushButton::clicked, this, &GetInfoDialog::onChangeDefaultAppClicked);

    btnRow->addStretch();
    btnRow->addWidget(m_changeAllBtn);
    boxLayout->addLayout(btnRow);

    layout->addWidget(openWithBox);
}

void GetInfoDialog::setupPermissionsSection(QVBoxLayout* layout, const FileItem& item)
{
    auto* permBox = new QGroupBox(QStringLiteral("Sharing & Permissions:"), this);
    auto* vLayout = new QVBoxLayout(permBox);
    vLayout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_permStringLabel = new QLabel(item.permissionsString(), permBox);
    QFont monoFont(QStringLiteral("monospace"));
    m_permStringLabel->setFont(monoFont);
    form->addRow(QStringLiteral("Access:"), m_permStringLabel);
    form->addRow(QStringLiteral("Owner:"), new QLabel(item.owner(), permBox));
    form->addRow(QStringLiteral("Group:"), new QLabel(item.group(), permBox));
    vLayout->addLayout(form);

    // Interactive permission grid
    auto* grid = new QGridLayout();
    grid->setSpacing(6);

    grid->addWidget(new QLabel(QStringLiteral(""), permBox), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Read"), permBox), 0, 1, Qt::AlignCenter);
    grid->addWidget(new QLabel(QStringLiteral("Write"), permBox), 0, 2, Qt::AlignCenter);
    grid->addWidget(new QLabel(QStringLiteral("Execute"), permBox), 0, 3, Qt::AlignCenter);

    mode_t mode = item.posixMode();

    auto makeCb = [this, permBox](bool checked) {
        auto* cb = new QCheckBox(permBox);
        cb->setChecked(checked);
        connect(cb, &QCheckBox::toggled, this, &GetInfoDialog::onPermissionCheckboxChanged);
        return cb;
    };

    grid->addWidget(new QLabel(QStringLiteral("Owner:"), permBox), 1, 0);
    m_ownerR = makeCb(mode & S_IRUSR);
    m_ownerW = makeCb(mode & S_IWUSR);
    m_ownerX = makeCb(mode & S_IXUSR);
    grid->addWidget(m_ownerR, 1, 1, Qt::AlignCenter);
    grid->addWidget(m_ownerW, 1, 2, Qt::AlignCenter);
    grid->addWidget(m_ownerX, 1, 3, Qt::AlignCenter);

    grid->addWidget(new QLabel(QStringLiteral("Group:"), permBox), 2, 0);
    m_groupR = makeCb(mode & S_IRGRP);
    m_groupW = makeCb(mode & S_IWGRP);
    m_groupX = makeCb(mode & S_IXGRP);
    grid->addWidget(m_groupR, 2, 1, Qt::AlignCenter);
    grid->addWidget(m_groupW, 2, 2, Qt::AlignCenter);
    grid->addWidget(m_groupX, 2, 3, Qt::AlignCenter);

    grid->addWidget(new QLabel(QStringLiteral("Others:"), permBox), 3, 0);
    m_otherR = makeCb(mode & S_IROTH);
    m_otherW = makeCb(mode & S_IWOTH);
    m_otherX = makeCb(mode & S_IXOTH);
    grid->addWidget(m_otherR, 3, 1, Qt::AlignCenter);
    grid->addWidget(m_otherW, 3, 2, Qt::AlignCenter);
    grid->addWidget(m_otherX, 3, 3, Qt::AlignCenter);

    vLayout->addLayout(grid);
    layout->addWidget(permBox);
}

void GetInfoDialog::onChangeDefaultAppClicked()
{
    QString selectedId = m_appCombo->currentData().toString();
    QString appName = m_appCombo->currentText();
    if (selectedId.isEmpty()) return;

    QString typeDesc = m_item.suffix().isEmpty() ? m_item.kind() : QStringLiteral(".%1").arg(m_item.suffix());

    auto res = QMessageBox::question(this, QStringLiteral("Change Default Application"),
        QStringLiteral("Are you sure you want to change all similar documents to open with the application \"%1\"?\n\nThis change will apply to all files of type %2.")
            .arg(appName, typeDesc),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (res == QMessageBox::Yes) {
        if (DesktopAppsManager::setDefaultAppForMime(m_item.mimeTypeName(), selectedId)) {
            QMessageBox::information(this, QStringLiteral("Default Application Updated"),
                QStringLiteral("Default application for %1 has been changed to %2.").arg(typeDesc, appName));
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("Could not update default application."));
        }
    }
}

void GetInfoDialog::onPermissionCheckboxChanged()
{
    mode_t newMode = 0;
    if (m_ownerR && m_ownerR->isChecked()) newMode |= S_IRUSR;
    if (m_ownerW && m_ownerW->isChecked()) newMode |= S_IWUSR;
    if (m_ownerX && m_ownerX->isChecked()) newMode |= S_IXUSR;

    if (m_groupR && m_groupR->isChecked()) newMode |= S_IRGRP;
    if (m_groupW && m_groupW->isChecked()) newMode |= S_IWGRP;
    if (m_groupX && m_groupX->isChecked()) newMode |= S_IXGRP;

    if (m_otherR && m_otherR->isChecked()) newMode |= S_IROTH;
    if (m_otherW && m_otherW->isChecked()) newMode |= S_IWOTH;
    if (m_otherX && m_otherX->isChecked()) newMode |= S_IXOTH;

    QByteArray localPath = QFile::encodeName(m_path);
    if (chmod(localPath.constData(), newMode) == 0) {
        m_item.refresh();
        m_permStringLabel->setText(m_item.permissionsString());
    }
}

void GetInfoDialog::onFolderSizeCalculated(qint64 totalBytes, int fileCount, int folderCount)
{
    m_sizeValueLabel->setText(QStringLiteral("%1 (%2 items)")
        .arg(FileItem::formatSize(totalBytes))
        .arg(fileCount + folderCount));
}
