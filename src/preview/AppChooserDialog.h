#pragma once

#include "DesktopAppsManager.h"
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QCheckBox>
#include <QPushButton>

class AppChooserDialog : public QDialog {
    Q_OBJECT

public:
    explicit AppChooserDialog(const QString& mimeType, const QString& fileName, QWidget* parent = nullptr);

    [[nodiscard]] QString selectedAppId() const;
    [[nodiscard]] bool makeDefault() const;

private slots:
    void onSearchFilterChanged(const QString& query);
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void populateApps();

    QString m_mimeType;
    QLineEdit* m_searchFilter{nullptr};
    QListWidget* m_appList{nullptr};
    QCheckBox* m_alwaysUseBox{nullptr};
    QPushButton* m_openBtn{nullptr};
    QVector<AppEntry> m_apps;
};
