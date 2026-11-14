#include "GetInfoDialog.h"
#include "FileMetadataManager.h"
#include "MediaMetadata.h"
#include "TagManager.h"
#include "WrappingLabel.h"
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <linux/fs.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace {

// File listings use extension-only MIME detection for speed; resolve the exact
// type here, where it matters for "Open with".
QString resolvedMimeType(const FileItem& item) {
    if (item.isDir())
        return QStringLiteral("inode/directory");
    if (item.isSymLink())
        return QStringLiteral("inode/symlink");
    QMimeDatabase database;
    return database.mimeTypeForFile(item.path(), QMimeDatabase::MatchDefault).name();
}

QStringList readXattrNames(const QString& path, bool symlink) {
    const QByteArray localPath = QFile::encodeName(path);
    const ssize_t size = symlink ? ::llistxattr(localPath.constData(), nullptr, 0)
                                 : ::listxattr(localPath.constData(), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    QByteArray buffer(static_cast<int>(size), '\0');
    const ssize_t readSize =
        symlink
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

bool getImmutableFlag(const QString& path, bool* locked) {
    const QByteArray localPath = QFile::encodeName(path);
    const int fd = ::open(localPath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    int flags = 0;
    const bool ok = ::ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0;
    ::close(fd);
    if (!ok)
        return false;
    *locked = (flags & FS_IMMUTABLE_FL) != 0;
    return true;
}

bool setImmutableFlag(const QString& path, bool locked, QString* error) {
    const QByteArray localPath = QFile::encodeName(path);
    const int fd = ::open(localPath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    int flags = 0;
    if (::ioctl(fd, FS_IOC_GETFLAGS, &flags) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        ::close(fd);
        return false;
    }
    if (locked)
        flags |= FS_IMMUTABLE_FL;
    else
        flags &= ~FS_IMMUTABLE_FL;
    const bool ok = ::ioctl(fd, FS_IOC_SETFLAGS, &flags) == 0;
    if (!ok && error)
        *error = QString::fromLocal8Bit(std::strerror(errno));
    ::close(fd);
    return ok;
}

QByteArray readXattrValue(const QString& path, const char* name, bool symlink) {
    const QByteArray localPath = QFile::encodeName(path);
    const ssize_t size = symlink ? ::lgetxattr(localPath.constData(), name, nullptr, 0)
                                 : ::getxattr(localPath.constData(), name, nullptr, 0);
    if (size < 0) {
        return {};
    }

    QByteArray value(static_cast<int>(size), '\0');
    if (size > 0) {
        const ssize_t readSize = symlink ? ::lgetxattr(localPath.constData(), name, value.data(),
                                                       static_cast<size_t>(value.size()))
                                         : ::getxattr(localPath.constData(), name, value.data(),
                                                      static_cast<size_t>(value.size()));
        if (readSize < 0) {
            return {};
        }
        value.resize(static_cast<int>(readSize));
    }
    return value;
}

QString xattrDisplayValue(const QByteArray& value, bool* binary = nullptr) {
    if (binary)
        *binary = false;
    if (value.isEmpty())
        return QStringLiteral("(empty)");
    if (value.contains('\0')) {
        if (binary)
            *binary = true;
        return QStringLiteral("[binary: %1 bytes]").arg(value.size());
    }
    const QString text = QString::fromUtf8(value);
    if (text.contains(QChar::ReplacementCharacter)) {
        if (binary)
            *binary = true;
        return QStringLiteral("[binary: %1 bytes]").arg(value.size());
    }
    return text;
}

} // namespace

FolderSizeWorker::FolderSizeWorker(QString folderPath) : m_folderPath(std::move(folderPath)) {}

void FolderSizeWorker::cancel() {
    m_cancelled.store(true, std::memory_order_relaxed);
}

void FolderSizeWorker::calculate() {
    qint64 totalBytes = 0;
    int files = 0;
    int folders = 0;

    QDirIterator it(m_folderPath,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (m_cancelled.load(std::memory_order_relaxed))
            return;
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
    : GetInfoDialog(QStringList{path}, parent) {}

GetInfoDialog::GetInfoDialog(const QStringList& paths, QWidget* parent) : QDialog(parent) {
    for (const QString& path : paths) {
        if (path.isEmpty())
            continue;
        FileItem item(path);
        if (!item.exists() && !item.isSymLink())
            continue;
        m_paths.append(item.path());
        m_items.append(item);
    }
    if (m_items.isEmpty()) {
        m_isMulti = false;
        m_item = FileItem();
    } else {
        m_isMulti = m_items.size() > 1;
        m_item = m_items.first();
        m_path = m_item.path();
        m_hasCommonMode = true;
        m_commonMode = m_item.posixMode() & 0777;
        for (const FileItem& item : m_items) {
            if ((item.posixMode() & 0777) != m_commonMode) {
                m_hasCommonMode = false;
                break;
            }
        }
    }

    if (m_isMulti) {
        setWindowTitle(QStringLiteral("%1 Items Info").arg(m_items.size()));
    } else if (!m_path.isEmpty()) {
        setWindowTitle(QStringLiteral("%1 Info").arg(m_item.fileName()));
    } else {
        setWindowTitle(QStringLiteral("Info"));
    }
    setFixedWidth(480);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(14);
    mainLayout->setContentsMargins(18, 18, 18, 18);

    // Top Header: Icon + Name
    auto* header = new QHBoxLayout();
    header->setSpacing(14);

    auto* iconLabel = new QLabel(this);
    iconLabel->setPixmap(
        (m_items.isEmpty() ? QIcon::fromTheme(QStringLiteral("dialog-information")) : m_item.icon())
            .pixmap(64, 64));
    header->addWidget(iconLabel);

    auto* nameLayout = new QVBoxLayout();
    QString displayName;
    QString displayKind;
    if (m_isMulti) {
        int folders = 0;
        int files = 0;
        for (const FileItem& item : m_items) {
            if (item.isDir())
                ++folders;
            else
                ++files;
        }
        displayName = QStringLiteral("%1 Items").arg(m_items.size());
        displayKind = QStringLiteral("%1 folder%2, %3 file%4")
                          .arg(folders)
                          .arg(folders == 1 ? QString() : QStringLiteral("s"))
                          .arg(files)
                          .arg(files == 1 ? QString() : QStringLiteral("s"));
    } else {
        displayName = m_path.isEmpty() ? QStringLiteral("No item") : m_item.fileName();
        displayKind = m_path.isEmpty() ? QString() : m_item.kind();
    }

    auto* nameLabel = new QLabel(displayName, this);
    QFont f = nameLabel->font();
    f.setBold(true);
    f.setPointSize(12);
    nameLabel->setFont(f);
    nameLabel->setWordWrap(true);

    auto* kindSub = new QLabel(displayKind, this);
    kindSub->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(kindSub);
    header->addLayout(nameLayout);
    header->addStretch();
    mainLayout->addLayout(header);

    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->setUsesScrollButtons(false);

    auto* generalPage = new QWidget(tabs);
    auto* generalLayout = new QVBoxLayout(generalPage);
    generalLayout->setContentsMargins(8, 10, 8, 8);
    generalLayout->setSpacing(12);
    setupGeneralSection(generalLayout, m_item);
    if (!m_isMulti) {
        setupMetadataSection(generalLayout, m_item);
    }
    setupTagsSection(generalLayout, m_paths);
    generalLayout->addStretch();
    tabs->addTab(generalPage, QStringLiteral("General"));

    if (!m_isMulti && !m_path.isEmpty()) {
        auto* metadataPage = new QWidget(tabs);
        auto* metadataLayout = new QVBoxLayout(metadataPage);
        metadataLayout->setContentsMargins(8, 10, 8, 8);
        setupDetailedMetadataTab(metadataLayout, m_item);
        metadataLayout->addStretch();
        tabs->addTab(metadataPage, QStringLiteral("Metadata"));
    }

    if (!m_path.isEmpty()) {
        auto* permissionsPage = new QWidget(tabs);
        auto* permissionsLayout = new QVBoxLayout(permissionsPage);
        permissionsLayout->setContentsMargins(8, 10, 8, 8);
        setupPermissionsSection(permissionsLayout, m_item);
        permissionsLayout->addStretch();
        tabs->addTab(permissionsPage, QStringLiteral("Permissions"));
    }

    if (!m_isMulti && !m_item.isDir() && !m_path.isEmpty()) {
        auto* appsPage = new QWidget(tabs);
        auto* appsLayout = new QVBoxLayout(appsPage);
        appsLayout->setContentsMargins(8, 10, 8, 8);
        setupOpenWithSection(appsLayout, m_item);
        appsLayout->addStretch();
        tabs->addTab(appsPage, QStringLiteral("Open With"));

        auto* checksumPage = new QWidget(tabs);
        auto* checksumLayout = new QVBoxLayout(checksumPage);
        checksumLayout->setContentsMargins(8, 10, 8, 8);
        setupChecksumSection(checksumLayout, m_item);
        checksumLayout->addStretch();
        tabs->addTab(checksumPage, QStringLiteral("Checksums"));
    }

    mainLayout->addWidget(tabs, 1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    mainLayout->addLayout(bottomRow);

    // Calculate folder size asynchronously for a single folder.
    if (!m_isMulti && m_item.isDir()) {
        m_workerThread = new QThread(this);
        m_worker = new FolderSizeWorker(m_path);
        m_worker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::started, m_worker, &FolderSizeWorker::calculate);
        connect(m_worker, &FolderSizeWorker::calculated, this,
                &GetInfoDialog::onFolderSizeCalculated);
        connect(m_worker, &FolderSizeWorker::calculated, m_workerThread, &QThread::quit);

        m_workerThread->start();
    } else if (m_isMulti) {
        QStringList folders;
        qint64 fileBytes = 0;
        for (const FileItem& selected : m_items) {
            if (selected.isDir())
                folders.append(selected.path());
            else
                fileBytes += selected.size();
        }
        if (!folders.isEmpty()) {
            m_multiSizeWatcher = new QFutureWatcher<QPair<qint64, int>>(this);
            connect(m_multiSizeWatcher, &QFutureWatcher<QPair<qint64, int>>::finished, this,
                    [this, fileBytes]() {
                        const auto result = m_multiSizeWatcher->result();
                        m_sizeValueLabel->setText(
                            QStringLiteral("%1 (%2 items)")
                                .arg(FileItem::formatSize(fileBytes + result.first))
                                .arg(m_items.size() + result.second));
                        m_multiSizeWatcher->deleteLater();
                        m_multiSizeWatcher = nullptr;
                    });
            m_multiSizeWatcher->setFuture(QtConcurrent::run([folders]() {
                qint64 total = 0;
                int count = 0;
                for (const QString& folder : folders) {
                    QDirIterator it(folder,
                                    QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                                        QDir::System,
                                    QDirIterator::Subdirectories);
                    while (it.hasNext()) {
                        it.next();
                        const QFileInfo info = it.fileInfo();
                        if (!info.isDir())
                            total += info.size();
                        ++count;
                    }
                }
                return qMakePair(total, count);
            }));
        }
    }
    // Calculate folder size asynchronously for a single folder.
    if (!m_isMulti && m_item.isDir()) {
        m_workerThread = new QThread(this);
        m_worker = new FolderSizeWorker(m_path);
        m_worker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::started, m_worker, &FolderSizeWorker::calculate);
        connect(m_worker, &FolderSizeWorker::calculated, this,
                &GetInfoDialog::onFolderSizeCalculated);
        connect(m_worker, &FolderSizeWorker::calculated, m_workerThread, &QThread::quit);

        m_workerThread->start();
    }
}

GetInfoDialog::~GetInfoDialog() {
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

void GetInfoDialog::setupGeneralSection(QVBoxLayout* layout, const FileItem& item) {
    auto* generalBox = new QGroupBox(QStringLiteral("General:"), this);
    auto* generalForm = new QFormLayout(generalBox);
    generalForm->setLabelAlignment(Qt::AlignRight);
    generalForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QString kind = item.kind();
    qint64 totalSize = 0;
    qint64 totalDiskUsage = 0;
    QSet<QString> kinds;
    QSet<QString> locations;
    QSet<QString> createdValues;
    QSet<QString> modifiedValues;
    for (const FileItem& selected : m_items) {
        kinds.insert(selected.kind());
        locations.insert(QFileInfo(selected.path()).absolutePath());
        createdValues.insert(selected.created().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        modifiedValues.insert(selected.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        if (!selected.isDir()) {
            totalSize += selected.size();
        }
        totalDiskUsage += selected.diskUsage();
    }
    if (m_isMulti) {
        kind = kinds.size() == 1 ? *kinds.constBegin() : QStringLiteral("Multiple");
    }
    generalForm->addRow(QStringLiteral("Kind:"), new QLabel(kind, generalBox));

    if (m_isMulti) {
        m_sizeValueLabel = new QLabel(QStringLiteral("%1 (%2 items)")
                                          .arg(FileItem::formatSize(totalSize))
                                          .arg(m_items.size()),
                                      generalBox);
    } else {
        m_sizeValueLabel =
            new QLabel(item.isDir() ? QStringLiteral("Calculating size...") : item.formattedSize(),
                       generalBox);
    }
    generalForm->addRow(QStringLiteral("Size:"), m_sizeValueLabel);

    if (m_isMulti || !item.isDir()) {
        generalForm->addRow(QStringLiteral("Disk Usage:"),
                            new QLabel(FileItem::formatSize(totalDiskUsage), generalBox));
    }

    QString where;
    if (!m_isMulti) {
        where = item.path();
    } else if (locations.size() == 1) {
        where = *locations.constBegin();
    } else {
        where = QStringLiteral("Multiple locations");
    }
    generalForm->addRow(QStringLiteral("Where:"), new WrappingLabel(where, generalBox));

    const QString created =
        createdValues.size() == 1 ? *createdValues.constBegin() : QStringLiteral("Multiple");
    const QString modified =
        modifiedValues.size() == 1 ? *modifiedValues.constBegin() : QStringLiteral("Multiple");
    generalForm->addRow(QStringLiteral("Created:"), new QLabel(created, generalBox));
    generalForm->addRow(QStringLiteral("Modified:"), new QLabel(modified, generalBox));

    if (!m_isMulti && item.isSymLink()) {
        auto* symlinkRow = new QHBoxLayout();
        QString target = item.symLinkTarget();
        bool exists =
            QFileInfo::exists(target) || QFileInfo(QDir(m_path).filePath(target)).exists();
        m_symlinkStatusLabel =
            new QLabel(exists ? QStringLiteral("Target: %1").arg(target)
                              : QStringLiteral("Target: %1 (Broken)").arg(target),
                       generalBox);
        m_symlinkStatusLabel->setStyleSheet(
            exists ? QString() : QStringLiteral("color: #f14c4c; font-weight: bold;"));
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

void GetInfoDialog::setupMetadataSection(QVBoxLayout* layout, const FileItem& item) {
    auto* metadataBox = new QGroupBox(QStringLiteral("Filesystem Metadata:"), this);
    auto* boxLayout = new QVBoxLayout(metadataBox);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    form->addRow(QStringLiteral("Inode:"), new QLabel(QString::number(item.inode()), metadataBox));
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

    const bool hasAcl = attributes.contains(QStringLiteral("system.posix_acl_access")) ||
                        attributes.contains(QStringLiteral("system.posix_acl_default"));
    form->addRow(
        QStringLiteral("POSIX ACL:"),
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
            const QString target = m_item.isDir() ? m_path : QFileInfo(m_path).absolutePath();
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
        connect(wallpaperBtn, &QPushButton::clicked, this,
                [this, wallpaperTool]() { QProcess::startDetached(wallpaperTool, {m_path}); });
        actions->addWidget(wallpaperBtn);
    }

    if (actions->count() > 0) {
        actions->addStretch();
        boxLayout->addLayout(actions);
    }

    layout->addWidget(metadataBox);
}

void GetInfoDialog::setupDetailedMetadataTab(QVBoxLayout* layout, const FileItem& item) {
    auto* infoBox = new QGroupBox(QStringLiteral("Dates & Version:"), this);
    auto* form = new QFormLayout(infoBox);
    form->setLabelAlignment(Qt::AlignRight);

    const QDateTime added = FileMetadataManager::instance().dateAdded(m_path);
    m_dateAddedValue =
        new QLabel(added.isValid() ? added.toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                                   : QStringLiteral("Unknown"),
                   infoBox);
    form->addRow(QStringLiteral("Date Added:"), m_dateAddedValue);

    const QDateTime opened = FileMetadataManager::instance().lastOpened(m_path);
    m_lastOpenedValue = new QLabel(
        (opened.isValid() ? opened : item.accessed()).toString(QStringLiteral("yyyy-MM-dd hh:mm")),
        infoBox);
    form->addRow(QStringLiteral("Last Opened:"), m_lastOpenedValue);

    m_versionEdit = new QLineEdit(item.version(), infoBox);
    m_versionEdit->setPlaceholderText(QStringLiteral("Optional version label"));
    connect(m_versionEdit, &QLineEdit::editingFinished, this, &GetInfoDialog::onVersionEdited);
    form->addRow(QStringLiteral("Version:"), m_versionEdit);

    m_commentEdit = new QLineEdit(item.comment(), infoBox);
    m_commentEdit->setPlaceholderText(QStringLiteral("Optional comment"));
    connect(m_commentEdit, &QLineEdit::editingFinished, this, &GetInfoDialog::onCommentEdited);
    form->addRow(QStringLiteral("Comments:"), m_commentEdit);
    layout->addWidget(infoBox);

    if (!item.isDir()) {
        const MediaMetadata media = MediaMetadataReader::inspect(m_path);
        if (!media.dimensions.isEmpty() || !media.duration.isEmpty() || !media.codec.isEmpty() ||
            !media.bitrate.isEmpty() || !media.exif.isEmpty()) {
            auto* mediaBox = new QGroupBox(QStringLiteral("Media Metadata:"), this);
            auto* mediaForm = new QFormLayout(mediaBox);
            mediaForm->setLabelAlignment(Qt::AlignRight);
            if (!media.dimensions.isEmpty())
                mediaForm->addRow(QStringLiteral("Dimensions:"),
                                  new QLabel(media.dimensions, mediaBox));
            if (!media.duration.isEmpty())
                mediaForm->addRow(QStringLiteral("Duration:"),
                                  new QLabel(media.duration, mediaBox));
            if (!media.codec.isEmpty())
                mediaForm->addRow(QStringLiteral("Codec:"), new QLabel(media.codec, mediaBox));
            if (!media.bitrate.isEmpty())
                mediaForm->addRow(QStringLiteral("Bitrate:"), new QLabel(media.bitrate, mediaBox));
            for (const QString& line : media.exif) {
                const qsizetype colon = line.indexOf(QLatin1Char(':'));
                if (colon > 0) {
                    mediaForm->addRow(line.left(colon + 1),
                                      new QLabel(line.mid(colon + 1).trimmed(), mediaBox));
                }
            }
            layout->addWidget(mediaBox);
        }
    }

    auto* xattrBox = new QGroupBox(QStringLiteral("Extended Attributes:"), this);
    auto* xattrLayout = new QVBoxLayout(xattrBox);
    m_xattrTable = new QTableWidget(0, 3, xattrBox);
    m_xattrTable->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Value"), QStringLiteral("Size")});
    m_xattrTable->horizontalHeader()->setStretchLastSection(false);
    m_xattrTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_xattrTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_xattrTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_xattrTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_xattrTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_xattrTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_xattrTable->setMinimumHeight(140);
    xattrLayout->addWidget(m_xattrTable);

    auto* xattrButtons = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("Add..."), xattrBox);
    auto* editBtn = new QPushButton(QStringLiteral("Edit..."), xattrBox);
    auto* removeBtn = new QPushButton(QStringLiteral("Remove"), xattrBox);
    connect(addBtn, &QPushButton::clicked, this, &GetInfoDialog::onAddXattr);
    connect(editBtn, &QPushButton::clicked, this, &GetInfoDialog::onEditXattr);
    connect(removeBtn, &QPushButton::clicked, this, &GetInfoDialog::onRemoveXattr);
    xattrButtons->addWidget(addBtn);
    xattrButtons->addWidget(editBtn);
    xattrButtons->addWidget(removeBtn);
    xattrButtons->addStretch();
    xattrLayout->addLayout(xattrButtons);
    layout->addWidget(xattrBox);

    refreshXattrTable();
}

void GetInfoDialog::refreshXattrTable() {
    if (!m_xattrTable)
        return;
    const QStringList names = readXattrNames(m_path, m_item.isSymLink());
    const int count = static_cast<int>(names.size());
    m_xattrTable->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        const QString& name = names.at(row);
        const QByteArray value =
            readXattrValue(m_path, name.toUtf8().constData(), m_item.isSymLink());
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, name);
        auto* valueItem = new QTableWidgetItem(xattrDisplayValue(value));
        auto* sizeItem = new QTableWidgetItem(QString::number(value.size()));
        m_xattrTable->setItem(row, 0, nameItem);
        m_xattrTable->setItem(row, 1, valueItem);
        m_xattrTable->setItem(row, 2, sizeItem);
    }
}

void GetInfoDialog::onVersionEdited() {
    if (!m_versionEdit)
        return;
    if (!m_item.setVersion(m_versionEdit->text())) {
        QMessageBox::warning(this, QStringLiteral("Version Not Saved"),
                             QStringLiteral("Could not write the version metadata."));
        m_versionEdit->setText(m_item.version());
    }
}

void GetInfoDialog::onCommentEdited() {
    if (!m_commentEdit)
        return;
    if (!m_item.setComment(m_commentEdit->text())) {
        QMessageBox::warning(this, QStringLiteral("Comment Not Saved"),
                             QStringLiteral("Could not write the comment metadata."));
        m_commentEdit->setText(m_item.comment());
    }
}

void GetInfoDialog::onAddXattr() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Extended Attribute"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(QStringLiteral("user.example"));
    auto* valueEdit = new QLineEdit(&dialog);
    form->addRow(QStringLiteral("Name:"), nameEdit);
    form->addRow(QStringLiteral("Value:"), valueEdit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || nameEdit->text().trimmed().isEmpty())
        return;

    const QByteArray name = nameEdit->text().trimmed().toUtf8();
    const QByteArray value = valueEdit->text().toUtf8();
    const QByteArray path = QFile::encodeName(m_path);
    const int rc = m_item.isSymLink()
                       ? ::lsetxattr(path.constData(), name.constData(), value.constData(),
                                     static_cast<size_t>(value.size()), 0)
                       : ::setxattr(path.constData(), name.constData(), value.constData(),
                                    static_cast<size_t>(value.size()), 0);
    if (rc != 0) {
        QMessageBox::warning(this, QStringLiteral("Extended Attribute"),
                             QString::fromLocal8Bit(std::strerror(errno)));
        return;
    }
    refreshXattrTable();
}

void GetInfoDialog::onEditXattr() {
    if (!m_xattrTable)
        return;
    const int row = m_xattrTable->currentRow();
    if (row < 0)
        return;
    const QString name = m_xattrTable->item(row, 0)->data(Qt::UserRole).toString();
    const QByteArray value = readXattrValue(m_path, name.toUtf8().constData(), m_item.isSymLink());
    bool binary = false;
    const QString display = xattrDisplayValue(value, &binary);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit Extended Attribute"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* nameEdit = new QLineEdit(name, &dialog);
    auto* valueEdit = new QLineEdit(binary ? QString() : display, &dialog);
    if (binary)
        valueEdit->setPlaceholderText(QStringLiteral("Binary value will be replaced"));
    form->addRow(QStringLiteral("Name:"), nameEdit);
    form->addRow(QStringLiteral("Value:"), valueEdit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QByteArray path = QFile::encodeName(m_path);
    const QByteArray oldName = name.toUtf8();
    if (nameEdit->text().trimmed() != name) {
        m_item.isSymLink() ? ::lremovexattr(path.constData(), oldName.constData())
                           : ::removexattr(path.constData(), oldName.constData());
    }
    const QByteArray newName = nameEdit->text().trimmed().toUtf8();
    const QByteArray newValue = valueEdit->text().toUtf8();
    const int rc = m_item.isSymLink()
                       ? ::lsetxattr(path.constData(), newName.constData(), newValue.constData(),
                                     static_cast<size_t>(newValue.size()), 0)
                       : ::setxattr(path.constData(), newName.constData(), newValue.constData(),
                                    static_cast<size_t>(newValue.size()), 0);
    if (rc != 0) {
        QMessageBox::warning(this, QStringLiteral("Extended Attribute"),
                             QString::fromLocal8Bit(std::strerror(errno)));
    }
    refreshXattrTable();
}

void GetInfoDialog::onRemoveXattr() {
    if (!m_xattrTable)
        return;
    const int row = m_xattrTable->currentRow();
    if (row < 0)
        return;
    const QString name = m_xattrTable->item(row, 0)->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, QStringLiteral("Remove Extended Attribute"),
                              QStringLiteral("Remove \"%1\"?").arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    const QByteArray path = QFile::encodeName(m_path);
    const QByteArray bytes = name.toUtf8();
    const int rc = m_item.isSymLink() ? ::lremovexattr(path.constData(), bytes.constData())
                                      : ::removexattr(path.constData(), bytes.constData());
    if (rc != 0 && errno != ENODATA) {
        QMessageBox::warning(this, QStringLiteral("Extended Attribute"),
                             QString::fromLocal8Bit(std::strerror(errno)));
    }
    refreshXattrTable();
}

void GetInfoDialog::setupChecksumSection(QVBoxLayout* layout, const FileItem& item) {
    Q_UNUSED(item);

    auto* checksumBox = new QGroupBox(QStringLiteral("Checksums:"), this);
    auto* boxLayout = new QVBoxLayout(checksumBox);

    auto* description =
        new QLabel(QStringLiteral("SHA-256 checksum of the file contents."), checksumBox);
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
                m_checksumCopyBtn->setEnabled(!result.isEmpty() &&
                                              !result.startsWith(QStringLiteral("Error")));
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

    connect(m_checksumCopyBtn, &QPushButton::clicked, this,
            [this]() { QApplication::clipboard()->setText(m_checksumEdit->text()); });

    layout->addWidget(checksumBox);
}

void GetInfoDialog::setupTagsSection(QVBoxLayout* layout, const QStringList& paths) {
    auto* tagBox = new QGroupBox(QStringLiteral("Tags:"), this);
    auto* tagLayout = new QHBoxLayout(tagBox);
    tagLayout->setSpacing(8);
    tagLayout->setContentsMargins(12, 10, 12, 10);

    for (const auto& tagInfo : TagManager::standardTags()) {
        bool allHaveTag = !paths.isEmpty();
        for (const QString& path : paths) {
            if (!TagManager::instance().hasTag(path, tagInfo.name)) {
                allHaveTag = false;
                break;
            }
        }

        auto* btn = new QToolButton(tagBox);
        btn->setFixedSize(24, 24);
        btn->setCheckable(true);
        btn->setChecked(allHaveTag);
        btn->setToolTip(tagInfo.name);

        auto updateStyle = [btn, color = tagInfo.color]() {
            bool checked = btn->isChecked();
            btn->setStyleSheet(QStringLiteral("QToolButton {"
                                              "  border-radius: 12px;"
                                              "  background-color: %1;"
                                              "  border: %2;"
                                              "}")
                                   .arg(color.name())
                                   .arg(checked ? QStringLiteral("3px solid #ffffff")
                                                : QStringLiteral("1px solid rgba(0,0,0,0.25)")));
        };
        updateStyle();

        connect(btn, &QToolButton::clicked, this, [paths, tag = tagInfo.name, btn, updateStyle]() {
            const bool remove = btn->isChecked();
            for (const QString& path : paths) {
                if (remove) {
                    TagManager::instance().addTag(path, tag);
                } else {
                    TagManager::instance().removeTag(path, tag);
                }
            }
            updateStyle();
        });

        tagLayout->addWidget(btn);
    }
    tagLayout->addStretch();
    layout->addWidget(tagBox);
}

void GetInfoDialog::setupOpenWithSection(QVBoxLayout* layout, const FileItem& item) {
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
            .arg(item.suffix().isEmpty() ? item.kind() : QStringLiteral(".%1").arg(item.suffix())),
        openWithBox);
    descLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    descLabel->setWordWrap(true);
    boxLayout->addWidget(descLabel);

    auto* btnRow = new QHBoxLayout();
    m_changeAllBtn = new QPushButton(QStringLiteral("Change All..."), openWithBox);
    m_changeAllBtn->setToolTip(
        QStringLiteral("Set this application as default for all files of this type"));
    connect(m_changeAllBtn, &QPushButton::clicked, this, &GetInfoDialog::onChangeDefaultAppClicked);

    btnRow->addStretch();
    btnRow->addWidget(m_changeAllBtn);
    boxLayout->addLayout(btnRow);

    layout->addWidget(openWithBox);
}

void GetInfoDialog::setupPermissionsSection(QVBoxLayout* layout, const FileItem& item) {
    Q_UNUSED(item);
    auto* permBox = new QGroupBox(QStringLiteral("Sharing & Permissions:"), this);
    auto* vLayout = new QVBoxLayout(permBox);
    vLayout->setSpacing(8);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_permStringLabel = new QLabel(permBox);
    QFont monoFont(QStringLiteral("monospace"));
    m_permStringLabel->setFont(monoFont);

    m_octalEdit = new QLineEdit(permBox);
    m_octalEdit->setFixedWidth(60);
    m_octalEdit->setMaxLength(4);
    m_octalEdit->setPlaceholderText(QStringLiteral("mixed"));
    connect(m_octalEdit, &QLineEdit::textEdited, this, &GetInfoDialog::onOctalEdited);

    auto* octalRow = new QHBoxLayout();
    octalRow->addWidget(m_permStringLabel);
    octalRow->addSpacing(10);
    octalRow->addWidget(new QLabel(QStringLiteral("Octal:"), permBox));
    octalRow->addWidget(m_octalEdit);
    octalRow->addStretch();

    form->addRow(QStringLiteral("Access:"), octalRow);

    auto* ownerGroupRow = new QHBoxLayout();
    ownerGroupRow->setSpacing(6);
    m_ownerEdit = new QLineEdit(permBox);
    m_ownerEdit->setFixedWidth(100);
    m_ownerEdit->setPlaceholderText(QStringLiteral("owner"));
    m_groupEdit = new QLineEdit(permBox);
    m_groupEdit->setFixedWidth(100);
    m_groupEdit->setPlaceholderText(QStringLiteral("group"));
    m_applyOwnershipBtn = new QPushButton(QStringLiteral("Apply"), permBox);
    m_applyOwnershipBtn->setFixedWidth(64);
    ownerGroupRow->addWidget(new QLabel(QStringLiteral("Owner:"), permBox));
    ownerGroupRow->addWidget(m_ownerEdit);
    ownerGroupRow->addWidget(new QLabel(QStringLiteral("Group:"), permBox));
    ownerGroupRow->addWidget(m_groupEdit);
    ownerGroupRow->addWidget(m_applyOwnershipBtn);
    ownerGroupRow->addStretch();
    form->addRow(QStringLiteral("Ownership:"), ownerGroupRow);
    connect(m_applyOwnershipBtn, &QPushButton::clicked, this, &GetInfoDialog::onApplyOwnership);

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
    m_ownerR = makeCb(false);
    m_ownerW = makeCb(false);
    m_ownerX = makeCb(false);
    grid->addWidget(m_ownerR, 1, 1, Qt::AlignCenter);
    grid->addWidget(m_ownerW, 1, 2, Qt::AlignCenter);
    grid->addWidget(m_ownerX, 1, 3, Qt::AlignCenter);

    grid->addWidget(new QLabel(QStringLiteral("Group:"), permBox), 2, 0);
    m_groupR = makeCb(false);
    m_groupW = makeCb(false);
    m_groupX = makeCb(false);
    grid->addWidget(m_groupR, 2, 1, Qt::AlignCenter);
    grid->addWidget(m_groupW, 2, 2, Qt::AlignCenter);
    grid->addWidget(m_groupX, 2, 3, Qt::AlignCenter);

    grid->addWidget(new QLabel(QStringLiteral("Others:"), permBox), 3, 0);
    m_otherR = makeCb(false);
    m_otherW = makeCb(false);
    m_otherX = makeCb(false);
    grid->addWidget(m_otherR, 3, 1, Qt::AlignCenter);
    grid->addWidget(m_otherW, 3, 2, Qt::AlignCenter);
    grid->addWidget(m_otherX, 3, 3, Qt::AlignCenter);

    vLayout->addLayout(grid);

    m_lockedCheck = new QCheckBox(QStringLiteral("Locked (immutable)"), permBox);
    m_lockedCheck->setTristate(true);
    m_lockedCheck->setToolTip(
        QStringLiteral("Prevents modification, deletion, or renaming. Requires ownership and may "
                       "require administrator privileges."));
    connect(m_lockedCheck, &QCheckBox::clicked, this, &GetInfoDialog::onLockToggled);
    vLayout->addWidget(m_lockedCheck);

    const bool hasDirectory =
        std::any_of(m_items.cbegin(), m_items.cend(),
                    [](const FileItem& selected) { return selected.isDir(); });
    if (hasDirectory) {
        m_applyRecursiveBtn =
            new QPushButton(QStringLiteral("Apply Permissions to Enclosed Items..."), permBox);
        connect(m_applyRecursiveBtn, &QPushButton::clicked, this,
                &GetInfoDialog::onApplyRecursivePermissions);
        vLayout->addWidget(m_applyRecursiveBtn, 0, Qt::AlignRight);

        m_recursiveOwnershipBtn =
            new QPushButton(QStringLiteral("Apply Ownership to Enclosed Items..."), permBox);
        connect(m_recursiveOwnershipBtn, &QPushButton::clicked, this,
                &GetInfoDialog::onApplyRecursiveOwnership);
        vLayout->addWidget(m_recursiveOwnershipBtn, 0, Qt::AlignRight);
    }

    const QString getfacl = QStandardPaths::findExecutable(QStringLiteral("getfacl"));
    const QString setfacl = QStandardPaths::findExecutable(QStringLiteral("setfacl"));
    if (!m_isMulti && !m_item.isSymLink() && !getfacl.isEmpty() && !setfacl.isEmpty()) {
        m_editAclBtn = new QPushButton(QStringLiteral("Edit ACL..."), permBox);
        connect(m_editAclBtn, &QPushButton::clicked, this, &GetInfoDialog::onEditAcl);
        vLayout->addWidget(m_editAclBtn, 0, Qt::AlignRight);
    }

    layout->addWidget(permBox);
    refreshPermissionUi();
}

Qt::CheckState
GetInfoDialog::commonCheckState(const std::function<bool(const FileItem&)>& predicate) const {
    if (m_items.isEmpty())
        return Qt::Unchecked;
    const bool first = predicate(m_items.first());
    for (const FileItem& item : m_items) {
        if (predicate(item) != first)
            return Qt::PartiallyChecked;
    }
    return first ? Qt::Checked : Qt::Unchecked;
}

void GetInfoDialog::refreshPermissionUi() {
    if (!m_permStringLabel)
        return;
    m_updatingPermissions = true;

    for (FileItem& selected : m_items) {
        selected.refresh();
    }
    if (!m_items.isEmpty()) {
        m_item = m_items.first();
    }

    m_hasCommonMode = !m_items.isEmpty();
    if (!m_items.isEmpty()) {
        m_commonMode = m_items.first().posixMode() & 0777;
        for (const FileItem& selected : m_items) {
            if ((selected.posixMode() & 0777) != m_commonMode) {
                m_hasCommonMode = false;
                break;
            }
        }
    }

    if (m_hasCommonMode) {
        const mode_t typeBits = m_items.first().posixMode() & static_cast<mode_t>(~07777u);
        m_permStringLabel->setText(FileItem::formatPermissions(typeBits | m_commonMode));
        m_octalEdit->setText(QString::number(m_commonMode, 8).rightJustified(3, QLatin1Char('0')));
    } else {
        m_permStringLabel->setText(QStringLiteral("Mixed permissions"));
        m_octalEdit->clear();
    }

    const mode_t mode = m_hasCommonMode ? m_commonMode : 0;
    if (m_ownerR)
        m_ownerR->setChecked(m_hasCommonMode && (mode & S_IRUSR));
    if (m_ownerW)
        m_ownerW->setChecked(m_hasCommonMode && (mode & S_IWUSR));
    if (m_ownerX)
        m_ownerX->setChecked(m_hasCommonMode && (mode & S_IXUSR));
    if (m_groupR)
        m_groupR->setChecked(m_hasCommonMode && (mode & S_IRGRP));
    if (m_groupW)
        m_groupW->setChecked(m_hasCommonMode && (mode & S_IWGRP));
    if (m_groupX)
        m_groupX->setChecked(m_hasCommonMode && (mode & S_IXGRP));
    if (m_otherR)
        m_otherR->setChecked(m_hasCommonMode && (mode & S_IROTH));
    if (m_otherW)
        m_otherW->setChecked(m_hasCommonMode && (mode & S_IWOTH));
    if (m_otherX)
        m_otherX->setChecked(m_hasCommonMode && (mode & S_IXOTH));

    QSet<QString> owners;
    QSet<QString> groups;
    for (const FileItem& selected : m_items) {
        owners.insert(selected.owner());
        groups.insert(selected.group());
    }
    m_ownerEdit->setText(owners.size() == 1 ? *owners.constBegin() : QString());
    m_ownerEdit->setPlaceholderText(owners.size() == 1 ? QStringLiteral("owner")
                                                       : QStringLiteral("multiple"));
    m_groupEdit->setText(groups.size() == 1 ? *groups.constBegin() : QString());
    m_groupEdit->setPlaceholderText(groups.size() == 1 ? QStringLiteral("group")
                                                       : QStringLiteral("multiple"));

    if (m_lockedCheck) {
        const Qt::CheckState lockedState = commonCheckState([](const FileItem& selected) {
            bool locked = false;
            return getImmutableFlag(selected.path(), &locked) && locked;
        });
        m_lockedCheck->setCheckState(lockedState);
    }

    m_updatingPermissions = false;
}

void GetInfoDialog::applyModeToAll(mode_t mode) {
    int failures = 0;
    for (const QString& path : m_paths) {
        const QFileInfo info(path);
        if (info.isSymLink())
            continue;
        const QByteArray localPath = QFile::encodeName(path);
        if (::chmod(localPath.constData(), mode) != 0) {
            ++failures;
        }
    }
    refreshPermissionUi();
    if (failures > 0) {
        QMessageBox::warning(
            this, QStringLiteral("Permissions Not Applied"),
            QStringLiteral("Could not change permissions on %1 item(s).").arg(failures));
    }
}

void GetInfoDialog::onApplyOwnership() {
    const QString ownerText = m_ownerEdit ? m_ownerEdit->text().trimmed() : QString();
    const QString groupText = m_groupEdit ? m_groupEdit->text().trimmed() : QString();
    if (ownerText.isEmpty() && groupText.isEmpty())
        return;

    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
    if (!ownerText.isEmpty()) {
        const QByteArray name = ownerText.toLocal8Bit();
        if (struct passwd* pw = ::getpwnam(name.constData())) {
            uid = pw->pw_uid;
        } else {
            QMessageBox::warning(this, QStringLiteral("Unknown Owner"),
                                 QStringLiteral("No user named \"%1\".").arg(ownerText));
            return;
        }
    }
    if (!groupText.isEmpty()) {
        const QByteArray name = groupText.toLocal8Bit();
        if (struct group* gr = ::getgrnam(name.constData())) {
            gid = gr->gr_gid;
        } else {
            QMessageBox::warning(this, QStringLiteral("Unknown Group"),
                                 QStringLiteral("No group named \"%1\".").arg(groupText));
            return;
        }
    }

    int failures = 0;
    QString lastError;
    for (const QString& path : m_paths) {
        const QByteArray localPath = QFile::encodeName(path);
        const bool symlink = QFileInfo(path).isSymLink();
        errno = 0;
        if ((symlink ? ::lchown(localPath.constData(), uid, gid)
                     : ::chown(localPath.constData(), uid, gid)) != 0) {
            ++failures;
            lastError = QString::fromLocal8Bit(std::strerror(errno));
        }
    }
    refreshPermissionUi();
    if (failures > 0) {
        QMessageBox::warning(
            this, QStringLiteral("Ownership Not Changed"),
            QStringLiteral("Could not change ownership on %1 item(s): %2\n\n"
                           "Changing ownership usually requires administrator privileges.")
                .arg(failures)
                .arg(lastError));
    }
}

void GetInfoDialog::onApplyRecursiveOwnership() {
    const QString ownerText = m_ownerEdit ? m_ownerEdit->text().trimmed() : QString();
    const QString groupText = m_groupEdit ? m_groupEdit->text().trimmed() : QString();
    if (ownerText.isEmpty() && groupText.isEmpty())
        return;

    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
    if (!ownerText.isEmpty()) {
        const QByteArray name = ownerText.toLocal8Bit();
        if (struct passwd* pw = ::getpwnam(name.constData()))
            uid = pw->pw_uid;
    }
    if (!groupText.isEmpty()) {
        const QByteArray name = groupText.toLocal8Bit();
        if (struct group* gr = ::getgrnam(name.constData()))
            gid = gr->gr_gid;
    }
    if (uid == static_cast<uid_t>(-1) && gid == static_cast<gid_t>(-1)) {
        QMessageBox::warning(this, QStringLiteral("Ownership Not Changed"),
                             QStringLiteral("Enter a valid owner or group first."));
        return;
    }

    if (QMessageBox::question(
            this, QStringLiteral("Apply Ownership"),
            QStringLiteral("Apply this ownership to all enclosed items in the selected folders?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    int successCount = 0;
    int failures = 0;
    for (const QString& path : m_paths) {
        if (!QFileInfo(path).isDir())
            continue;
        QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isSymLink())
                continue;
            const QByteArray localPath = QFile::encodeName(info.absoluteFilePath());
            if (::chown(localPath.constData(), uid, gid) == 0)
                ++successCount;
            else
                ++failures;
        }
    }
    refreshPermissionUi();
    if (failures > 0) {
        QMessageBox::warning(this, QStringLiteral("Ownership Applied"),
                             QStringLiteral("Changed %1 items. Failed on %2 items.")
                                 .arg(successCount)
                                 .arg(failures));
    } else {
        QMessageBox::information(
            this, QStringLiteral("Ownership Applied"),
            QStringLiteral("Changed ownership on %1 enclosed items.").arg(successCount));
    }
}

void GetInfoDialog::onLockToggled(bool /*locked*/) {
    if (m_updatingPermissions || !m_lockedCheck)
        return;
    if (m_lockedCheck->checkState() == Qt::PartiallyChecked) {
        m_lockedCheck->setCheckState(Qt::Checked);
    }
    const bool locked = m_lockedCheck->checkState() == Qt::Checked;

    int failures = 0;
    QString lastError;
    for (const QString& path : m_paths) {
        if (QFileInfo(path).isSymLink())
            continue;
        QString error;
        if (!setImmutableFlag(path, locked, &error)) {
            ++failures;
            lastError = error;
        }
    }
    refreshPermissionUi();
    if (failures > 0) {
        QMessageBox::warning(this, QStringLiteral("Lock Not Changed"),
                             QStringLiteral("Could not %1 %2 item(s): %3\n\n"
                                            "This operation may require administrator privileges.")
                                 .arg(locked ? QStringLiteral("lock") : QStringLiteral("unlock"))
                                 .arg(failures)
                                 .arg(lastError));
    }
}

void GetInfoDialog::onEditAcl() {
    const QString getfacl = QStandardPaths::findExecutable(QStringLiteral("getfacl"));
    const QString setfacl = QStandardPaths::findExecutable(QStringLiteral("setfacl"));
    if (getfacl.isEmpty() || setfacl.isEmpty() || m_path.isEmpty())
        return;

    QProcess readProcess;
    readProcess.start(getfacl, {QStringLiteral("-cp"), m_path});
    if (!readProcess.waitForFinished(5000) || readProcess.exitCode() != 0) {
        QMessageBox::warning(this, QStringLiteral("ACL Error"),
                             QString::fromLocal8Bit(readProcess.readAllStandardError()).trimmed());
        return;
    }
    const QString original = QString::fromLocal8Bit(readProcess.readAllStandardOutput());

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit ACL - %1").arg(m_item.fileName()));
    dialog.resize(600, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* edit = new QPlainTextEdit(&dialog);
    edit->setPlainText(original);
    edit->setFont(QFont(QStringLiteral("monospace"), 9));
    layout->addWidget(edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QProcess writeProcess;
    writeProcess.start(setfacl, {QStringLiteral("--set-file=-"), m_path});
    if (!writeProcess.waitForStarted(3000)) {
        QMessageBox::warning(this, QStringLiteral("ACL Error"),
                             QStringLiteral("Could not start setfacl."));
        return;
    }
    writeProcess.write(edit->toPlainText().toLocal8Bit());
    writeProcess.closeWriteChannel();
    if (!writeProcess.waitForFinished(5000) || writeProcess.exitCode() != 0) {
        QMessageBox::warning(this, QStringLiteral("ACL Error"),
                             QString::fromLocal8Bit(writeProcess.readAllStandardError()).trimmed());
        return;
    }
    QMessageBox::information(this, QStringLiteral("ACL Updated"),
                             QStringLiteral("The ACL for this item was updated."));
}

void GetInfoDialog::onChangeDefaultAppClicked() {
    QString selectedId = m_appCombo->currentData().toString();
    QString appName = m_appCombo->currentText();
    if (selectedId.isEmpty())
        return;

    QString typeDesc =
        m_item.suffix().isEmpty() ? m_item.kind() : QStringLiteral(".%1").arg(m_item.suffix());

    auto res = QMessageBox::question(
        this, QStringLiteral("Change Default Application"),
        QStringLiteral("Are you sure you want to change all similar documents to open with the "
                       "application \"%1\"?\n\nThis change will apply to all files of type %2.")
            .arg(appName, typeDesc),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (res == QMessageBox::Yes) {
        if (DesktopAppsManager::setDefaultAppForMime(resolvedMimeType(m_item), selectedId)) {
            QMessageBox::information(
                this, QStringLiteral("Default Application Updated"),
                QStringLiteral("Default application for %1 has been changed to %2.")
                    .arg(typeDesc, appName));
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"),
                                 QStringLiteral("Could not update default application."));
        }
    }
}

void GetInfoDialog::onPermissionCheckboxChanged() {
    if (m_updatingPermissions)
        return;

    mode_t newMode = 0;
    auto checked = [](QCheckBox* box) { return box && box->checkState() == Qt::Checked; };
    if (checked(m_ownerR))
        newMode |= S_IRUSR;
    if (checked(m_ownerW))
        newMode |= S_IWUSR;
    if (checked(m_ownerX))
        newMode |= S_IXUSR;
    if (checked(m_groupR))
        newMode |= S_IRGRP;
    if (checked(m_groupW))
        newMode |= S_IWGRP;
    if (checked(m_groupX))
        newMode |= S_IXGRP;
    if (checked(m_otherR))
        newMode |= S_IROTH;
    if (checked(m_otherW))
        newMode |= S_IWOTH;
    if (checked(m_otherX))
        newMode |= S_IXOTH;

    applyModeToAll(newMode);
}

void GetInfoDialog::onOctalEdited(const QString& text) {
    if (m_updatingPermissions)
        return;
    bool ok = false;
    const uint value = text.toUInt(&ok, 8);
    if (!ok || value > 0777)
        return;
    applyModeToAll(static_cast<mode_t>(value));
}

void GetInfoDialog::onApplyRecursivePermissions() {
    if (m_paths.isEmpty())
        return;
    if (!m_hasCommonMode) {
        QMessageBox::warning(
            this, QStringLiteral("Permissions Not Applied"),
            QStringLiteral("Set a common permission value before applying it recursively."));
        return;
    }

    int folderCount = 0;
    for (const QString& path : m_paths) {
        if (QFileInfo(path).isDir())
            ++folderCount;
    }
    if (folderCount == 0)
        return;

    auto res = QMessageBox::question(
        this, QStringLiteral("Apply Permissions"),
        QStringLiteral(
            "Apply permissions %1 to all enclosed files and folders in %2 selected folder(s)?")
            .arg(FileItem::formatPermissions(S_IFDIR | m_commonMode))
            .arg(folderCount),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (res != QMessageBox::Yes)
        return;

    const mode_t mode = m_commonMode;
    int successCount = 0;
    int errorCount = 0;

    for (const QString& path : m_paths) {
        if (!QFileInfo(path).isDir())
            continue;
        QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isSymLink())
                continue;
            mode_t targetMode = mode;
            if (fi.isDir()) {
                if (targetMode & S_IRUSR)
                    targetMode |= S_IXUSR;
                if (targetMode & S_IRGRP)
                    targetMode |= S_IXGRP;
                if (targetMode & S_IROTH)
                    targetMode |= S_IXOTH;
            }
            const QByteArray p = QFile::encodeName(fi.absoluteFilePath());
            if (::chmod(p.constData(), targetMode) == 0) {
                ++successCount;
            } else {
                ++errorCount;
            }
        }
    }

    if (errorCount > 0) {
        QMessageBox::warning(
            this, QStringLiteral("Permissions Applied"),
            QStringLiteral(
                "Applied permissions to %1 items. Failed on %2 items (permission denied).")
                .arg(successCount)
                .arg(errorCount));
    } else {
        QMessageBox::information(
            this, QStringLiteral("Permissions Applied"),
            QStringLiteral("Successfully applied permissions to %1 enclosed items.")
                .arg(successCount));
    }
}

void GetInfoDialog::onRelinkClicked() {
    if (!m_item.isSymLink())
        return;

    bool ok = false;
    QString currentTarget = m_item.symLinkTarget();
    QString newTarget = QInputDialog::getText(this, QStringLiteral("Relink Symlink"),
                                              QStringLiteral("Enter new target destination:"),
                                              QLineEdit::Normal, currentTarget, &ok);
    if (!ok || newTarget.isEmpty() || newTarget == currentTarget)
        return;

    QByteArray localPath = QFile::encodeName(m_path);
    QByteArray targetBytes = QFile::encodeName(newTarget);
    if (unlink(localPath.constData()) != 0) {
        QMessageBox::warning(this, QStringLiteral("Relink Failed"),
                             QStringLiteral("Could not remove existing symlink: %1")
                                 .arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    if (symlink(targetBytes.constData(), localPath.constData()) != 0) {
        QMessageBox::warning(this, QStringLiteral("Relink Failed"),
                             QStringLiteral("Could not create new symlink: %1")
                                 .arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    m_item.refresh();
    QString target = m_item.symLinkTarget();
    bool exists = QFileInfo::exists(target) ||
                  QFileInfo(QDir(QFileInfo(m_path).absolutePath()).filePath(target)).exists();
    if (m_symlinkStatusLabel) {
        m_symlinkStatusLabel->setText(exists ? QStringLiteral("Target: %1").arg(target)
                                             : QStringLiteral("Target: %1 (Broken)").arg(target));
        m_symlinkStatusLabel->setStyleSheet(
            exists ? QString() : QStringLiteral("color: #f14c4c; font-weight: bold;"));
    }
}

void GetInfoDialog::onFolderSizeCalculated(qint64 totalBytes, int fileCount, int folderCount) {
    m_sizeValueLabel->setText(QStringLiteral("%1 (%2 items)")
                                  .arg(FileItem::formatSize(totalBytes))
                                  .arg(fileCount + folderCount));
}
