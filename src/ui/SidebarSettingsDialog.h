#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QVector>

class SidebarSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SidebarSettingsDialog(QWidget* parent = nullptr);

signals:
    void sidebarChanged();

private slots:
    void onPinFolderClicked();
    void onRestoreDefaultsClicked();

private:
    void setupUi();
    void populateFavoritesList();
    void populateLocationsList();

    QListWidget* m_favsList{nullptr};
    QListWidget* m_locationsList{nullptr};
};
