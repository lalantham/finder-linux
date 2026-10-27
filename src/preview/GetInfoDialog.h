#pragma once

#include "FileItem.h"
#include <QDialog>
#include <QLabel>
#include <QThread>
#include <atomic>

class FolderSizeWorker : public QObject {
    Q_OBJECT

public:
    explicit FolderSizeWorker(QString folderPath);
    void cancel();

public slots:
    void calculate();

signals:
    void calculated(qint64 totalBytes, int fileCount, int folderCount);

private:
    QString m_folderPath;
    std::atomic<bool> m_cancelled{false};
};

class GetInfoDialog : public QDialog {
    Q_OBJECT

public:
    explicit GetInfoDialog(const QString& path, QWidget* parent = nullptr);
    ~GetInfoDialog() override;

private slots:
    void onFolderSizeCalculated(qint64 totalBytes, int fileCount, int folderCount);

private:
    QString m_path;
    QLabel* m_sizeValueLabel{nullptr};
    QThread* m_workerThread{nullptr};
    FolderSizeWorker* m_worker{nullptr};
};
