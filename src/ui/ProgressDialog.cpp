#include "ProgressDialog.h"
#include "FileItem.h"
#include <QAbstractTableModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableView>
#include <QVBoxLayout>

namespace {
QString statusText(int status) {
    switch (static_cast<OperationItemStatus>(status)) {
    case OperationItemStatus::Pending:
        return QStringLiteral("Pending");
    case OperationItemStatus::Copying:
        return QStringLiteral("Copying");
    case OperationItemStatus::Moving:
        return QStringLiteral("Moving");
    case OperationItemStatus::Compressing:
        return QStringLiteral("Compressing");
    case OperationItemStatus::Extracting:
        return QStringLiteral("Extracting");
    case OperationItemStatus::Trashing:
        return QStringLiteral("Trashing");
    case OperationItemStatus::Deleting:
        return QStringLiteral("Deleting");
    case OperationItemStatus::Done:
        return QStringLiteral("Done");
    case OperationItemStatus::Failed:
        return QStringLiteral("Failed");
    case OperationItemStatus::Skipped:
        return QStringLiteral("Skipped");
    case OperationItemStatus::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Pending");
}
} // namespace

class OperationItemModel : public QAbstractTableModel {
public:
    explicit OperationItemModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_items.size());
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : 2;
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
            return {};
        const OperationItem& item = m_items.at(index.row());
        if (role == Qt::DisplayRole) {
            return index.column() == 0 ? item.displayName
                                       : statusText(static_cast<int>(item.status));
        }
        if (role == Qt::ToolTipRole) {
            return item.detail.isEmpty() ? item.sourcePath : item.detail;
        }
        if (role == Qt::TextAlignmentRole && index.column() == 1)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        return section == 0 ? QStringLiteral("Item") : QStringLiteral("Status");
    }

    void resetItems(const QList<OperationItem>& items) {
        beginResetModel();
        m_items = items;
        m_rows.clear();
        for (int i = 0; i < m_items.size(); ++i)
            m_rows.insert(m_items.at(i).id, i);
        endResetModel();
    }

    void updateItem(quint64 id, int status, const QString& detail, qint64 processedBytes) {
        const int row = m_rows.value(id, -1);
        if (row < 0 || row >= m_items.size())
            return;
        OperationItem& item = m_items[row];
        item.status = static_cast<OperationItemStatus>(status);
        item.detail = detail;
        item.processedBytes = processedBytes;
        emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    }

    [[nodiscard]] OperationItem itemAt(int row) const {
        return (row >= 0 && row < m_items.size()) ? m_items.at(row) : OperationItem{};
    }
    [[nodiscard]] int count() const {
        return static_cast<int>(m_items.size());
    }
    void clear() {
        beginResetModel();
        m_items.clear();
        m_rows.clear();
        endResetModel();
    }

private:
    QList<OperationItem> m_items;
    QHash<quint64, int> m_rows;
};

ProgressDialog::ProgressDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("File Operation"));
    setFixedWidth(560);
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

    m_itemModel = new OperationItemModel(this);
    m_itemView = new QTableView(this);
    m_itemView->setModel(m_itemModel);
    m_itemView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_itemView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_itemView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_itemView->setShowGrid(false);
    m_itemView->setAlternatingRowColors(true);
    m_itemView->verticalHeader()->setVisible(false);
    m_itemView->verticalHeader()->setDefaultSectionSize(20);
    m_itemView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_itemView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_itemView->setMaximumHeight(150);
    m_itemView->hide();
    layout->addWidget(m_itemView);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    m_summaryLabel->hide();
    layout->addWidget(m_summaryLabel);

    auto* bottomRow = new QHBoxLayout();
    m_detailLabel = new QLabel(this);
    m_cancelItemBtn = new QPushButton(QStringLiteral("Cancel Item"), this);
    m_cancelItemBtn->setFixedWidth(90);
    m_cancelItemBtn->setEnabled(false);
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
    bottomRow->addWidget(m_cancelItemBtn);
    bottomRow->addWidget(m_skipFileBtn);
    bottomRow->addWidget(m_skipBtn);
    bottomRow->addWidget(m_pauseBtn);
    bottomRow->addWidget(m_cancelBtn);
    layout->addLayout(bottomRow);

    connect(m_cancelItemBtn, &QPushButton::clicked, this, [this]() {
        const QModelIndex current = m_itemView->currentIndex();
        if (!current.isValid())
            return;
        const OperationItem item = m_itemModel->itemAt(current.row());
        if (item.id != 0)
            emit cancelItemRequested(item.id);
    });
    connect(m_itemView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (m_finished) {
            m_cancelItemBtn->setEnabled(false);
            return;
        }
        const QModelIndex current = m_itemView->currentIndex();
        if (!current.isValid()) {
            m_cancelItemBtn->setEnabled(false);
            return;
        }
        const OperationItem item = m_itemModel->itemAt(current.row());
        m_cancelItemBtn->setEnabled(item.status == OperationItemStatus::Pending);
    });
    connect(m_skipFileBtn, &QPushButton::clicked, this, &ProgressDialog::skipCurrentFileRequested);
    connect(m_skipBtn, &QPushButton::clicked, this, &ProgressDialog::skipRequested);
    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_paused)
            emit resumeRequested();
        else
            emit pauseRequested();
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_finished)
            emit closeRequested();
        else
            emit cancelRequested();
    });
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
    m_itemModel->clear();
    m_itemView->hide();
    m_summaryLabel->clear();
    m_summaryLabel->hide();
    m_finished = false;
    m_cancelBtn->setText(QStringLiteral("Stop"));
    const bool pausable = type == OperationType::Copy || type == OperationType::Move ||
                          type == OperationType::Duplicate || type == OperationType::Trash ||
                          type == OperationType::Delete || type == OperationType::Compress ||
                          type == OperationType::Extract;
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

void ProgressDialog::setOperationItems(const QList<OperationItem>& items) {
    m_itemModel->resetItems(items);
    m_summaryLabel->hide();
    m_itemView->setVisible(items.size() > 1);
    m_cancelItemBtn->setEnabled(false);
    m_finished = false;
    m_cancelBtn->setText(QStringLiteral("Stop"));
}

void ProgressDialog::updateOperationItem(quint64 id, int status, const QString& detail,
                                         qint64 processedBytes) {
    m_itemModel->updateItem(id, status, detail, processedBytes);
    if (m_itemView->isVisible() && !m_itemView->currentIndex().isValid() &&
        m_itemModel->count() > 0) {
        m_itemView->setCurrentIndex(m_itemModel->index(0, 0));
    }
}

void ProgressDialog::finish(const OperationResult& result) {
    m_finished = true;
    m_summaryLabel->setText(
        QStringLiteral("Processed: %1   •   Failed: %2   •   Skipped: %3   •   Cancelled: %4")
            .arg(result.processedCount)
            .arg(result.errorCount)
            .arg(result.skippedCount)
            .arg(result.cancelledCount));
    m_summaryLabel->setToolTip(result.errorMessage);
    m_summaryLabel->show();
    m_pauseBtn->setEnabled(false);
    m_skipFileBtn->setEnabled(false);
    m_cancelItemBtn->setEnabled(false);
    m_cancelBtn->setText(QStringLiteral("Close"));
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
