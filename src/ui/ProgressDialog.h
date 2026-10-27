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

signals:
    void cancelRequested();

private:
    QLabel* m_titleLabel{nullptr};
    QLabel* m_detailLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QPushButton* m_cancelBtn{nullptr};
};
