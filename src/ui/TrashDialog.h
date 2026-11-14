#pragma once

#include "XdgTrash.h"
#include <QDialog>
#include <QVector>

class QTreeWidget;
class QLabel;
class QPushButton;

class TrashDialog : public QDialog {
    Q_OBJECT

public:
    explicit TrashDialog(QWidget* parent = nullptr);

signals:
    void trashChanged();

private slots:
    void reload();
    void restoreSelected();
    void deleteSelected();
    void emptyAll();
    void updateButtons();

private:
    QTreeWidget* m_tree{nullptr};
    QLabel* m_summary{nullptr};
    QPushButton* m_restoreBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QPushButton* m_emptyBtn{nullptr};
    QVector<TrashItem> m_items;
};
