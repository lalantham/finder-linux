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

    QWidget* m_favsContainer{nullptr};
    QListWidget* m_customFavsList{nullptr};
    QWidget* m_locationsContainer{nullptr};

    QVector<QPair<QString, QCheckBox*>> m_standardFavBoxes;
    QVector<QPair<QString, QCheckBox*>> m_locationBoxes;
};
