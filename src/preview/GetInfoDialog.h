#pragma once

#include "FileItem.h"
#include "DesktopAppsManager.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
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
    void onChangeDefaultAppClicked();
    void onPermissionCheckboxChanged();

private:
    void setupGeneralSection(QVBoxLayout* layout, const FileItem& item);
    void setupOpenWithSection(QVBoxLayout* layout, const FileItem& item);
    void setupPermissionsSection(QVBoxLayout* layout, const FileItem& item);

    QString m_path;
    FileItem m_item;

    QLabel* m_sizeValueLabel{nullptr};
    QComboBox* m_appCombo{nullptr};
    QPushButton* m_changeAllBtn{nullptr};
    QLabel* m_permStringLabel{nullptr};

    // Permission checkboxes
    QCheckBox* m_ownerR{nullptr};
    QCheckBox* m_ownerW{nullptr};
    QCheckBox* m_ownerX{nullptr};
    QCheckBox* m_groupR{nullptr};
    QCheckBox* m_groupW{nullptr};
    QCheckBox* m_groupX{nullptr};
    QCheckBox* m_otherR{nullptr};
    QCheckBox* m_otherW{nullptr};
    QCheckBox* m_otherX{nullptr};

    QThread* m_workerThread{nullptr};
    FolderSizeWorker* m_worker{nullptr};
};
