#pragma once

#include "FileOperation.h"
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

class ProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget* parent = nullptr);

    void setOperation(OperationType type, int totalItems, qint64 totalBytes);
    void updateProgress(const OperationProgress& progress);
    void updateQueueStatus(int currentIndex, int totalCount);
    void setPaused(bool paused);

signals:
    void cancelRequested();
    void skipRequested();
    void skipCurrentFileRequested();
    void pauseRequested();
    void resumeRequested();

private:
    QLabel* m_titleLabel{nullptr};
    QLabel* m_queueLabel{nullptr};
    QLabel* m_detailLabel{nullptr};
    QLabel* m_currentFileLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QPushButton* m_skipFileBtn{nullptr};
    QPushButton* m_skipBtn{nullptr};
    QPushButton* m_pauseBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    bool m_paused{false};
};
