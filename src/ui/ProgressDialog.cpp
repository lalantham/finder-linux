#include "ProgressDialog.h"
#include "FileItem.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

ProgressDialog::ProgressDialog(QWidget* parent) : QDialog(parent) {
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

    m_queueLabel = new QLabel(this);
    m_queueLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    m_queueLabel->hide();
    layout->addWidget(m_queueLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    layout->addWidget(m_progressBar);

    m_currentFileLabel = new QLabel(this);
    m_currentFileLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    m_currentFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_currentFileLabel->hide();
    layout->addWidget(m_currentFileLabel);

    auto* bottomRow = new QHBoxLayout();
    m_detailLabel = new QLabel(this);
    m_skipFileBtn = new QPushButton(QStringLiteral("Skip File"), this);
    m_skipFileBtn->setFixedWidth(80);
    m_skipBtn = new QPushButton(QStringLiteral("Skip Task"), this);
    m_skipBtn->setFixedWidth(80);
    m_skipBtn->hide();
    m_pauseBtn = new QPushButton(QStringLiteral("Pause"), this);
    m_pauseBtn->setFixedWidth(70);
    m_cancelBtn = new QPushButton(QStringLiteral("Stop"), this);
    m_cancelBtn->setFixedWidth(70);

    bottomRow->addWidget(m_detailLabel);
    bottomRow->addStretch();
    bottomRow->addWidget(m_skipFileBtn);
    bottomRow->addWidget(m_skipBtn);
    bottomRow->addWidget(m_pauseBtn);
    bottomRow->addWidget(m_cancelBtn);
    layout->addLayout(bottomRow);

    connect(m_skipFileBtn, &QPushButton::clicked, this, &ProgressDialog::skipCurrentFileRequested);
    connect(m_skipBtn, &QPushButton::clicked, this, &ProgressDialog::skipRequested);
    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_paused)
            emit resumeRequested();
        else
            emit pauseRequested();
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, &ProgressDialog::cancelRequested);
}

void ProgressDialog::setOperation(OperationType type, int totalItems, qint64 totalBytes) {
    QString action;
    switch (type) {
    case OperationType::Copy:
        action = QStringLiteral("Copying");
        break;
    case OperationType::Move:
        action = QStringLiteral("Moving");
        break;
    case OperationType::Duplicate:
        action = QStringLiteral("Duplicating");
        break;
    case OperationType::Trash:
        action = QStringLiteral("Moving to Trash");
        break;
    case OperationType::Delete:
        action = QStringLiteral("Deleting");
        break;
    case OperationType::Compress:
        action = QStringLiteral("Compressing");
        break;
    case OperationType::Extract:
        action = QStringLiteral("Extracting");
        break;
    default:
        action = QStringLiteral("Processing");
        break;
    }

    if (totalBytes > 0) {
        m_titleLabel->setText(QStringLiteral("%1 %2 items (%3)...")
                                  .arg(action)
                                  .arg(totalItems)
                                  .arg(FileItem::formatSize(totalBytes)));
    } else {
        m_titleLabel->setText(QStringLiteral("%1 %2 items...").arg(action).arg(totalItems));
    }
    m_progressBar->setValue(0);
    m_detailLabel->setText(QStringLiteral("0 of %1").arg(totalItems));
    m_currentFileLabel->clear();
    m_currentFileLabel->hide();
    const bool pausable = type == OperationType::Copy || type == OperationType::Move ||
                          type == OperationType::Duplicate || type == OperationType::Trash ||
                          type == OperationType::Delete;
    const bool skippable = type == OperationType::Copy || type == OperationType::Move ||
                           type == OperationType::Duplicate;
    m_pauseBtn->setEnabled(pausable);
    m_skipFileBtn->setEnabled(skippable);
    setPaused(false);
    show();
}

void ProgressDialog::updateProgress(const OperationProgress& progress) {
    m_progressBar->setValue(progress.percent);

    QString detail;
    if (progress.totalBytes > 0) {
        if (progress.totalItems > 1) {
            detail = QStringLiteral("%1 of %2 (%3 of %4)")
                         .arg(progress.itemsProcessed)
                         .arg(progress.totalItems)
                         .arg(FileItem::formatSize(progress.bytesProcessed))
                         .arg(FileItem::formatSize(progress.totalBytes));
        } else {
            detail = QStringLiteral("%1 of %2")
                         .arg(FileItem::formatSize(progress.bytesProcessed))
                         .arg(FileItem::formatSize(progress.totalBytes));
        }
    } else if (!progress.currentItem.isEmpty()) {
        detail = progress.currentItem;
    } else {
        detail = FileItem::formatSize(progress.bytesProcessed);
    }

    if (progress.speedBytesPerSec > 0.0) {
        detail += QStringLiteral(" • %1/s").arg(
            FileItem::formatSize(static_cast<qint64>(progress.speedBytesPerSec)));
    }
    if (progress.remainingMs >= 0) {
        const qint64 totalSeconds = progress.remainingMs / 1000;
        QString remaining;
        if (totalSeconds >= 3600) {
            remaining =
                QStringLiteral("%1h %2m").arg(totalSeconds / 3600).arg((totalSeconds % 3600) / 60);
        } else if (totalSeconds >= 60) {
            remaining = QStringLiteral("%1m %2s").arg(totalSeconds / 60).arg(totalSeconds % 60);
        } else {
            remaining = QStringLiteral("%1s").arg(totalSeconds);
        }
        detail += QStringLiteral(" • %1 left").arg(remaining);
    }
    m_detailLabel->setText(detail);

    if (!progress.currentItem.isEmpty()) {
        m_currentFileLabel->setText(progress.currentItem);
        m_currentFileLabel->setToolTip(progress.currentItem);
        m_currentFileLabel->show();
    }
}

void ProgressDialog::setPaused(bool paused) {
    m_paused = paused;
    m_pauseBtn->setText(paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    m_currentFileLabel->setProperty("paused", paused);
}

void ProgressDialog::updateQueueStatus(int currentIndex, int totalCount) {
    if (totalCount > 1 && currentIndex > 0) {
        m_queueLabel->setText(QStringLiteral("Task %1 of %2 (%3 queued)")
                                  .arg(currentIndex)
                                  .arg(totalCount)
                                  .arg(totalCount - currentIndex));
        m_queueLabel->show();
        m_skipBtn->show();
        m_cancelBtn->setText(QStringLiteral("Stop All"));
    } else {
        m_queueLabel->hide();
        m_skipBtn->hide();
        m_cancelBtn->setText(QStringLiteral("Stop"));
    }
}
