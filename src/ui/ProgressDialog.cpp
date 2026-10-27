#include "ProgressDialog.h"
#include "FileItem.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("File Operation"));
    setFixedWidth(400);
    setModal(false);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    m_titleLabel = new QLabel(QStringLiteral("Preparing..."), this);
    QFont font = m_titleLabel->font();
    font.setBold(true);
    m_titleLabel->setFont(font);
    layout->addWidget(m_titleLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    layout->addWidget(m_progressBar);

    auto* bottomRow = new QHBoxLayout();
    m_detailLabel = new QLabel(this);
    m_cancelBtn = new QPushButton(QStringLiteral("Stop"), this);
    m_cancelBtn->setFixedWidth(70);

    bottomRow->addWidget(m_detailLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(m_cancelBtn);
    layout->addLayout(bottomRow);

    connect(m_cancelBtn, &QPushButton::clicked, this, &ProgressDialog::cancelRequested);
}

void ProgressDialog::setOperation(OperationType type, int totalItems, qint64 totalBytes)
{
    QString action;
    switch (type) {
    case OperationType::Copy: action = QStringLiteral("Copying"); break;
    case OperationType::Move: action = QStringLiteral("Moving"); break;
    case OperationType::Duplicate: action = QStringLiteral("Duplicating"); break;
    case OperationType::Trash: action = QStringLiteral("Moving to Trash"); break;
    case OperationType::Delete: action = QStringLiteral("Deleting"); break;
    default: action = QStringLiteral("Processing"); break;
    }

    m_titleLabel->setText(QStringLiteral("%1 %2 items (%3)...").arg(action).arg(totalItems).arg(FileItem::formatSize(totalBytes)));
    m_progressBar->setValue(0);
    m_detailLabel->setText(QStringLiteral("0 of %1").arg(totalItems));
    show();
}

void ProgressDialog::updateProgress(const OperationProgress& progress)
{
    m_progressBar->setValue(progress.percent);
    m_detailLabel->setText(QStringLiteral("%1 of %2 (%3 of %4)")
        .arg(progress.itemsProcessed)
        .arg(progress.totalItems)
        .arg(FileItem::formatSize(progress.bytesProcessed))
        .arg(FileItem::formatSize(progress.totalBytes)));
}
