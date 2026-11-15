#pragma once

#include "FileOperation.h"
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

class OperationItemModel;
class QTableView;

class ProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget* parent = nullptr);

    void setOperation(OperationType type, int totalItems, qint64 totalBytes);
    void updateProgress(const OperationProgress& progress);
    void updateQueueStatus(int currentIndex, int totalCount);
    void setPaused(bool paused);
    void setOperationItems(const QList<OperationItem>& items);
    void updateOperationItem(quint64 id, int status, const QString& detail, qint64 processedBytes);
    void finish(const OperationResult& result);

signals:
    void cancelRequested();
    void skipRequested();
    void skipCurrentFileRequested();
    void pauseRequested();
    void resumeRequested();
    void cancelItemRequested(quint64 itemId);
    void closeRequested();

private:
    QLabel* m_titleLabel{nullptr};
    QLabel* m_queueLabel{nullptr};
    QLabel* m_detailLabel{nullptr};
    QLabel* m_currentFileLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QTableView* m_itemView{nullptr};
    OperationItemModel* m_itemModel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QPushButton* m_cancelItemBtn{nullptr};
    QPushButton* m_skipFileBtn{nullptr};
    QPushButton* m_skipBtn{nullptr};
    QPushButton* m_pauseBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    bool m_paused{false};
    bool m_finished{false};
};
