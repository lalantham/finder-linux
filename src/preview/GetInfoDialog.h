#pragma once

#include "DesktopAppsManager.h"
#include "FileItem.h"
#include "MediaMetadata.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QVector>
#include <atomic>
#include <functional>

class QFormLayout;
class QGroupBox;

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
    explicit GetInfoDialog(const QStringList& paths, QWidget* parent = nullptr);
    ~GetInfoDialog() override;

private slots:
    void onFolderSizeCalculated(qint64 totalBytes, int fileCount, int folderCount);
    void onChangeDefaultAppClicked();
    void onPermissionCheckboxChanged();
    void onOctalEdited(const QString& text);
    void onApplyRecursivePermissions();
    void onRelinkClicked();
    void onApplyOwnership();
    void onApplyRecursiveOwnership();
    void onLockToggled(bool locked);
    void onEditAcl();
    void onVersionEdited();
    void onCommentEdited();
    void onAddXattr();
    void onEditXattr();
    void onRemoveXattr();
    void onMediaMetadataReady();

private:
    void setupGeneralSection(QVBoxLayout* layout, const FileItem& item);
    void setupMetadataSection(QVBoxLayout* layout, const FileItem& item);
    void setupDetailedMetadataTab(QVBoxLayout* layout, const FileItem& item);
    void refreshXattrTable();
    void setupChecksumSection(QVBoxLayout* layout, const FileItem& item);
    void setupTagsSection(QVBoxLayout* layout, const QStringList& paths);
    void setupOpenWithSection(QVBoxLayout* layout, const FileItem& item);
    void setupPermissionsSection(QVBoxLayout* layout, const FileItem& item);
    void refreshPermissionUi();
    void applyModeToAll(mode_t mode);
    [[nodiscard]] Qt::CheckState
    commonCheckState(const std::function<bool(const FileItem&)>& predicate) const;

    QString m_path;
    FileItem m_item;
    QStringList m_paths;
    QVector<FileItem> m_items;
    bool m_isMulti{false};
    mode_t m_commonMode{0};
    bool m_hasCommonMode{false};

    QLabel* m_sizeValueLabel{nullptr};
    QComboBox* m_appCombo{nullptr};
    QPushButton* m_changeAllBtn{nullptr};
    QLabel* m_permStringLabel{nullptr};
    QLineEdit* m_octalEdit{nullptr};
    QPushButton* m_applyRecursiveBtn{nullptr};
    QLabel* m_symlinkStatusLabel{nullptr};
    QPushButton* m_relinkBtn{nullptr};
    QLineEdit* m_ownerEdit{nullptr};
    QLineEdit* m_groupEdit{nullptr};
    QPushButton* m_applyOwnershipBtn{nullptr};
    QPushButton* m_recursiveOwnershipBtn{nullptr};
    QCheckBox* m_lockedCheck{nullptr};
    QPushButton* m_editAclBtn{nullptr};
    QLineEdit* m_checksumEdit{nullptr};
    QPushButton* m_checksumBtn{nullptr};
    QPushButton* m_checksumCopyBtn{nullptr};
    QFutureWatcher<QString>* m_checksumWatcher{nullptr};
    QFutureWatcher<QPair<qint64, int>>* m_multiSizeWatcher{nullptr};
    QLineEdit* m_versionEdit{nullptr};
    QLineEdit* m_commentEdit{nullptr};
    QLabel* m_dateAddedValue{nullptr};
    QLabel* m_lastOpenedValue{nullptr};
    QTableWidget* m_xattrTable{nullptr};
    QGroupBox* m_mediaBox{nullptr};
    QFormLayout* m_mediaForm{nullptr};
    QFutureWatcher<MediaMetadata>* m_mediaWatcher{nullptr};
    bool m_updatingPermissions{false};

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
