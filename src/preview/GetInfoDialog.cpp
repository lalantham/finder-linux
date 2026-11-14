#include "GetInfoDialog.h"
#include "TagManager.h"
#include "WrappingLabel.h"
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
#include <QLineEdit>
#include <QInputDialog>
#include <QResizeEvent>
#include <QProcess>
#include <QStandardPaths>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QtConcurrent/QtConcurrentRun>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <cstring>

namespace {

// File listings use extension-only MIME detection for speed; resolve the exact
// type here, where it matters for "Open with".
QString resolvedMimeType(const FileItem& item)
{
    if (item.isDir()) return QStringLiteral("inode/directory");
    if (item.isSymLink()) return QStringLiteral("inode/symlink");
    QMimeDatabase database;
    return database.mimeTypeForFile(item.path(), QMimeDatabase::MatchDefault).name();
}

QStringList readXattrNames(const QString& path, bool symlink)
{
    const QByteArray localPath = QFile::encodeName(path);
    const ssize_t size = symlink
        ? ::llistxattr(localPath.constData(), nullptr, 0)
        : ::listxattr(localPath.constData(), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    QByteArray buffer(static_cast<int>(size), '\0');
    const ssize_t readSize = symlink
        ? ::llistxattr(localPath.constData(), buffer.data(), static_cast<size_t>(buffer.size()))
        : ::listxattr(localPath.constData(), buffer.data(), static_cast<size_t>(buffer.size()));
    if (readSize <= 0) {
        return {};
    }

    QStringList names;
    qsizetype offset = 0;
    while (offset < readSize) {
        const char* name = buffer.constData() + offset;
        const qsizetype length = static_cast<qsizetype>(std::strlen(name));
        if (length == 0) {
            break;
        }
        names.append(QString::fromUtf8(name, static_cast<int>(length)));
        offset += length + 1;
    }
    return names;
}

QByteArray readXattrValue(const QString& path, const char* name, bool symlink)
{
    const QByteArray localPath = QFile::encodeName(path);
    const ssize_t size = symlink
        ? ::lgetxattr(localPath.constData(), name, nullptr, 0)
        : ::getxattr(localPath.constData(), name, nullptr, 0);
    if (size < 0) {
        return {};
    }

    QByteArray value(static_cast<int>(size), '\0');
    if (size > 0) {
        const ssize_t readSize = symlink
            ? ::lgetxattr(localPath.constData(), name, value.data(), static_cast<size_t>(value.size()))
            : ::getxattr(localPath.constData(), name, value.data(), static_cast<size_t>(value.size()));
        if (readSize < 0) {
            return {};
        }
        value.resize(static_cast<int>(readSize));
    }
    return value;
}

} // namespace

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
    setupMetadataSection(mainLayout, m_item);
    setupTagsSection(mainLayout, m_path);

    if (!m_item.isDir()) {
        setupOpenWithSection(mainLayout, m_item);
        setupChecksumSection(mainLayout, m_item);
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
        // The worker polls the cancel flag, so it usually exits immediately.
        // Never terminate() it: that could corrupt the heap mid-walk.
        if (m_worker && m_workerThread->isRunning()) {
            connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
        }
        m_workerThread->quit();
        if (!m_workerThread->wait(3000)) {
            // Slow filesystem: detach so closing the dialog neither blocks nor
            // destroys a running QThread.
            m_workerThread->setParent(nullptr);
            connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
        }
    }
}

void GetInfoDialog::setupGeneralSection(QVBoxLayout* layout, const FileItem& item)
{
    auto* generalBox = new QGroupBox(QStringLiteral("General:"), this);
    auto* generalForm = new QFormLayout(generalBox);
    generalForm->setLabelAlignment(Qt::AlignRight);
    generalForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    generalForm->addRow(QStringLiteral("Kind:"), new QLabel(item.kind(), generalBox));

    m_sizeValueLabel = new QLabel(item.isDir() ? QStringLiteral("Calculating size...") : item.formattedSize(), generalBox);
    generalForm->addRow(QStringLiteral("Size:"), m_sizeValueLabel);

    if (!item.isDir()) {
        generalForm->addRow(QStringLiteral("Disk Usage:"), new QLabel(FileItem::formatSize(item.diskUsage()), generalBox));
    }

    auto* whereLabel = new WrappingLabel(item.path(), generalBox);
    generalForm->addRow(QStringLiteral("Where:"), whereLabel);

    generalForm->addRow(QStringLiteral("Created:"), new QLabel(item.created().toString(QStringLiteral("yyyy-MM-dd hh:mm")), generalBox));
    generalForm->addRow(QStringLiteral("Modified:"), new QLabel(item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm")), generalBox));

    if (item.isSymLink()) {
        auto* symlinkRow = new QHBoxLayout();
        QString target = item.symLinkTarget();
        bool exists = QFileInfo::exists(target) || QFileInfo(QDir(m_path).filePath(target)).exists();
        m_symlinkStatusLabel = new QLabel(exists ? QStringLiteral("Target: %1").arg(target)
                                                 : QStringLiteral("Target: %1 (Broken)").arg(target), generalBox);
        m_symlinkStatusLabel->setStyleSheet(exists ? QString() : QStringLiteral("color: #f14c4c; font-weight: bold;"));
        m_symlinkStatusLabel->setWordWrap(true);
        symlinkRow->addWidget(m_symlinkStatusLabel, 1);

        m_relinkBtn = new QPushButton(QStringLiteral("Relink..."), generalBox);
        m_relinkBtn->setFixedWidth(70);
        connect(m_relinkBtn, &QPushButton::clicked, this, &GetInfoDialog::onRelinkClicked);
        symlinkRow->addWidget(m_relinkBtn);
        generalForm->addRow(QStringLiteral("Original:"), symlinkRow);
    }

    layout->addWidget(generalBox);
}

void GetInfoDialog::setupMetadataSection(QVBoxLayout* layout, const FileItem& item)
{
    auto* metadataBox = new QGroupBox(QStringLiteral("Filesystem Metadata:"), this);
    auto* boxLayout = new QVBoxLayout(metadataBox);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    form->addRow(QStringLiteral("Inode:"),
                 new QLabel(QString::number(item.inode()), metadataBox));
    form->addRow(QStringLiteral("Device:"),
                 new QLabel(QStringLiteral("%1:%2")
                                .arg(major(static_cast<dev_t>(item.deviceId())))
                                .arg(minor(static_cast<dev_t>(item.deviceId()))),
                            metadataBox));
    form->addRow(QStringLiteral("Hard Links:"),
                 new QLabel(QString::number(item.hardLinks()), metadataBox));
    form->addRow(QStringLiteral("Disk Usage:"),
                 new QLabel(FileItem::formatSize(item.diskUsage()), metadataBox));

    const QStringList attributes = readXattrNames(m_path, item.isSymLink());
    auto* attributesLabel = new WrappingLabel(
        attributes.isEmpty() ? QStringLiteral("None") : attributes.join(QStringLiteral(", ")),
        metadataBox);
    form->addRow(QStringLiteral("Extended Attributes:"), attributesLabel);

    const bool hasAcl = attributes.contains(QStringLiteral("system.posix_acl_access"))
        || attributes.contains(QStringLiteral("system.posix_acl_default"));
    form->addRow(QStringLiteral("POSIX ACL:"),
                 new QLabel(hasAcl ? QStringLiteral("Present") : QStringLiteral("None"), metadataBox));

    if (attributes.contains(QStringLiteral("security.selinux"))) {
        QByteArray context = readXattrValue(m_path, "security.selinux", item.isSymLink());
        if (context.endsWith('\0')) {
            context.chop(1);
        }
        form->addRow(QStringLiteral("SELinux Context:"),
                     new QLabel(QString::fromUtf8(context), metadataBox));
    }

    boxLayout->addLayout(form);

    auto* actions = new QHBoxLayout();
    const QString filelight = QStandardPaths::findExecutable(QStringLiteral("filelight"));
    if (!filelight.isEmpty()) {
        auto* filelightBtn = new QPushButton(QStringLiteral("Show Disk Usage"), metadataBox);
        connect(filelightBtn, &QPushButton::clicked, this, [this, filelight]() {
            const QString target = m_item.isDir()
                ? m_path : QFileInfo(m_path).absolutePath();
            QProcess::startDetached(filelight, {target});
        });
        actions->addWidget(filelightBtn);
    }

    const QString wallpaperTool =
        QStandardPaths::findExecutable(QStringLiteral("plasma-apply-wallpaperimage"));
    QMimeDatabase mimeDatabase;
    const QString mimeType =
        mimeDatabase.mimeTypeForFile(m_path, QMimeDatabase::MatchDefault).name();
    if (!wallpaperTool.isEmpty() && mimeType.startsWith(QStringLiteral("image/"))) {
        auto* wallpaperBtn = new QPushButton(QStringLiteral("Set as Wallpaper"), metadataBox);
        connect(wallpaperBtn, &QPushButton::clicked, this, [this, wallpaperTool]() {
            QProcess::startDetached(wallpaperTool, {m_path});
        });
        actions->addWidget(wallpaperBtn);
    }

    if (actions->count() > 0) {
        actions->addStretch();
        boxLayout->addLayout(actions);
    }

    layout->addWidget(metadataBox);
}

void GetInfoDialog::setupChecksumSection(QVBoxLayout* layout, const FileItem& item)
{
    Q_UNUSED(item);

    auto* checksumBox = new QGroupBox(QStringLiteral("Checksums:"), this);
    auto* boxLayout = new QVBoxLayout(checksumBox);

    auto* description = new QLabel(
        QStringLiteral("SHA-256 checksum of the file contents."), checksumBox);
    description->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    boxLayout->addWidget(description);

    auto* row = new QHBoxLayout();
    m_checksumEdit = new QLineEdit(checksumBox);
    m_checksumEdit->setReadOnly(true);
    m_checksumEdit->setPlaceholderText(QStringLiteral("Not calculated"));
    m_checksumBtn = new QPushButton(QStringLiteral("Calculate"), checksumBox);
    m_checksumCopyBtn = new QPushButton(QStringLiteral("Copy"), checksumBox);
    m_checksumCopyBtn->setEnabled(false);
    row->addWidget(m_checksumEdit, 1);
    row->addWidget(m_checksumBtn);
    row->addWidget(m_checksumCopyBtn);
    boxLayout->addLayout(row);

    connect(m_checksumBtn, &QPushButton::clicked, this, [this]() {
        if (!m_checksumWatcher) {
            m_checksumWatcher = new QFutureWatcher<QString>(this);
            connect(m_checksumWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
                const QString result = m_checksumWatcher->result();
                m_checksumEdit->setText(result);
                m_checksumBtn->setEnabled(true);
                m_checksumCopyBtn->setEnabled(
                    !result.isEmpty() && !result.startsWith(QStringLiteral("Error")));
            });
        }
        if (m_checksumWatcher->isRunning()) {
            return;
        }

        m_checksumBtn->setEnabled(false);
        m_checksumCopyBtn->setEnabled(false);
        m_checksumEdit->setText(QStringLiteral("Calculating..."));
        const QString path = m_path;
        m_checksumWatcher->setFuture(QtConcurrent::run([path]() {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return QStringLiteral("Error: %1").arg(file.errorString());
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                return QStringLiteral("Error: could not read file");
            }
            return QString::fromLatin1(hash.result().toHex());
        }));
    });

    connect(m_checksumCopyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_checksumEdit->text());
    });

    layout->addWidget(checksumBox);
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

    QVector<AppEntry> apps = DesktopAppsManager::appsForMime(resolvedMimeType(item));
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

    mode_t mode = item.posixMode();

    m_permStringLabel = new QLabel(item.permissionsString(), permBox);
    QFont monoFont(QStringLiteral("monospace"));
    m_permStringLabel->setFont(monoFont);

    m_octalEdit = new QLineEdit(QString::number(mode & 0777, 8).rightJustified(3, '0'), permBox);
    m_octalEdit->setFixedWidth(60);
    m_octalEdit->setMaxLength(4);
    connect(m_octalEdit, &QLineEdit::textEdited, this, &GetInfoDialog::onOctalEdited);

    auto* octalRow = new QHBoxLayout();
    octalRow->addWidget(m_permStringLabel);
    octalRow->addSpacing(10);
    octalRow->addWidget(new QLabel(QStringLiteral("Octal:"), permBox));
    octalRow->addWidget(m_octalEdit);
    octalRow->addStretch();

    form->addRow(QStringLiteral("Access:"), octalRow);
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

    if (item.isDir()) {
        m_applyRecursiveBtn = new QPushButton(QStringLiteral("Apply to Enclosed Items..."), permBox);
        connect(m_applyRecursiveBtn, &QPushButton::clicked, this, &GetInfoDialog::onApplyRecursivePermissions);
        vLayout->addWidget(m_applyRecursiveBtn, 0, Qt::AlignRight);
    }

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
        if (DesktopAppsManager::setDefaultAppForMime(resolvedMimeType(m_item), selectedId)) {
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
    if (m_updatingPermissions) return;
    m_updatingPermissions = true;

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
        if (m_octalEdit) {
            m_octalEdit->setText(QString::number(newMode & 0777, 8).rightJustified(3, '0'));
        }
    }
    m_updatingPermissions = false;
}

void GetInfoDialog::onOctalEdited(const QString& text)
{
    if (m_updatingPermissions) return;
    bool ok = false;
    uint val = text.toUInt(&ok, 8);
    if (!ok || val > 0777) return;

    mode_t newMode = static_cast<mode_t>(val);
    QByteArray localPath = QFile::encodeName(m_path);
    if (chmod(localPath.constData(), newMode) == 0) {
        m_updatingPermissions = true;
        m_item.refresh();
        m_permStringLabel->setText(m_item.permissionsString());
        if (m_ownerR) m_ownerR->setChecked(newMode & S_IRUSR);
        if (m_ownerW) m_ownerW->setChecked(newMode & S_IWUSR);
        if (m_ownerX) m_ownerX->setChecked(newMode & S_IXUSR);
        if (m_groupR) m_groupR->setChecked(newMode & S_IRGRP);
        if (m_groupW) m_groupW->setChecked(newMode & S_IWGRP);
        if (m_groupX) m_groupX->setChecked(newMode & S_IXGRP);
        if (m_otherR) m_otherR->setChecked(newMode & S_IROTH);
        if (m_otherW) m_otherW->setChecked(newMode & S_IWOTH);
        if (m_otherX) m_otherX->setChecked(newMode & S_IXOTH);
        m_updatingPermissions = false;
    }
}

void GetInfoDialog::onApplyRecursivePermissions()
{
    if (!m_item.isDir()) return;

    auto res = QMessageBox::question(this, QStringLiteral("Apply Permissions"),
        QStringLiteral("Apply permissions %1 to all enclosed files and folders within \"%2\"?")
            .arg(m_item.permissionsString(), m_item.fileName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (res != QMessageBox::Yes) return;

    mode_t mode = m_item.posixMode() & 0777;
    int successCount = 0;
    int errorCount = 0;

    QDirIterator it(m_path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        if (fi.isSymLink()) continue;
        mode_t targetMode = mode;
        if (fi.isDir()) {
            if (targetMode & S_IRUSR) targetMode |= S_IXUSR;
            if (targetMode & S_IRGRP) targetMode |= S_IXGRP;
            if (targetMode & S_IROTH) targetMode |= S_IXOTH;
        }
        QByteArray p = QFile::encodeName(fi.absoluteFilePath());
        if (chmod(p.constData(), targetMode) == 0) {
            successCount++;
        } else {
            errorCount++;
        }
    }

    if (errorCount > 0) {
        QMessageBox::warning(this, QStringLiteral("Permissions Applied"),
            QStringLiteral("Applied permissions to %1 items. Failed on %2 items (permission denied).")
                .arg(successCount).arg(errorCount));
    } else {
        QMessageBox::information(this, QStringLiteral("Permissions Applied"),
            QStringLiteral("Successfully applied permissions to %1 enclosed items.").arg(successCount));
    }
}

void GetInfoDialog::onRelinkClicked()
{
    if (!m_item.isSymLink()) return;

    bool ok = false;
    QString currentTarget = m_item.symLinkTarget();
    QString newTarget = QInputDialog::getText(this, QStringLiteral("Relink Symlink"),
        QStringLiteral("Enter new target destination:"), QLineEdit::Normal, currentTarget, &ok);
    if (!ok || newTarget.isEmpty() || newTarget == currentTarget) return;

    QByteArray localPath = QFile::encodeName(m_path);
    QByteArray targetBytes = QFile::encodeName(newTarget);
    if (unlink(localPath.constData()) != 0) {
        QMessageBox::warning(this, QStringLiteral("Relink Failed"),
            QStringLiteral("Could not remove existing symlink: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    if (symlink(targetBytes.constData(), localPath.constData()) != 0) {
        QMessageBox::warning(this, QStringLiteral("Relink Failed"),
            QStringLiteral("Could not create new symlink: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    m_item.refresh();
    QString target = m_item.symLinkTarget();
    bool exists = QFileInfo::exists(target) || QFileInfo(QDir(QFileInfo(m_path).absolutePath()).filePath(target)).exists();
    if (m_symlinkStatusLabel) {
        m_symlinkStatusLabel->setText(exists ? QStringLiteral("Target: %1").arg(target)
                                             : QStringLiteral("Target: %1 (Broken)").arg(target));
        m_symlinkStatusLabel->setStyleSheet(exists ? QString() : QStringLiteral("color: #f14c4c; font-weight: bold;"));
    }
}

void GetInfoDialog::onFolderSizeCalculated(qint64 totalBytes, int fileCount, int folderCount)
{
    m_sizeValueLabel->setText(QStringLiteral("%1 (%2 items)")
        .arg(FileItem::formatSize(totalBytes))
        .arg(fileCount + folderCount));
}
