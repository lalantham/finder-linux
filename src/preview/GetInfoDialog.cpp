#include "GetInfoDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileInfo>
#include <QDirIterator>
#include <QPushButton>

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
{
    FileItem item(path);
    setWindowTitle(QStringLiteral("%1 Info").arg(item.fileName()));
    setFixedWidth(380);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(14);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Top Header: Icon + Name
    auto* header = new QHBoxLayout();
    header->setSpacing(12);

    auto* iconLabel = new QLabel(this);
    iconLabel->setPixmap(item.icon().pixmap(64, 64));
    header->addWidget(iconLabel);

    auto* nameLayout = new QVBoxLayout();
    auto* nameLabel = new QLabel(item.fileName(), this);
    QFont f = nameLabel->font();
    f.setBold(true);
    f.setPointSize(11);
    nameLabel->setFont(f);
    nameLabel->setWordWrap(true);

    auto* kindSub = new QLabel(item.kind(), this);
    kindSub->setStyleSheet(QStringLiteral("color: #777777; font-size: 11px;"));

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(kindSub);
    header->addLayout(nameLayout);
    header->addStretch();
    mainLayout->addLayout(header);

    // General Group
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

    mainLayout->addWidget(generalBox);

    // Sharing & Permissions Group
    auto* permBox = new QGroupBox(QStringLiteral("Sharing & Permissions:"), this);
    auto* permForm = new QFormLayout(permBox);
    permForm->setLabelAlignment(Qt::AlignRight);

    permForm->addRow(QStringLiteral("Permissions:"), new QLabel(item.permissionsString(), permBox));
    permForm->addRow(QStringLiteral("Owner:"), new QLabel(item.owner(), permBox));
    permForm->addRow(QStringLiteral("Group:"), new QLabel(item.group(), permBox));

    mainLayout->addWidget(permBox);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(closeBtn, 0, Qt::AlignRight);

    // Calculate folder size asynchronously
    if (item.isDir()) {
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

void GetInfoDialog::onFolderSizeCalculated(qint64 totalBytes, int fileCount, int folderCount)
{
    m_sizeValueLabel->setText(QStringLiteral("%1 (%2 items)")
        .arg(FileItem::formatSize(totalBytes))
        .arg(fileCount + folderCount));
}
