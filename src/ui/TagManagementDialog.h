#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;

class TagManagementDialog : public QDialog {
    Q_OBJECT

public:
    explicit TagManagementDialog(QWidget* parent = nullptr);

private slots:
    void refresh();
    void addTag();
    void renameTag();
    void removeTag();

private:
    [[nodiscard]] QString selectedTagName() const;
    [[nodiscard]] bool isStandardTag(const QString& name) const;

    QListWidget* m_list{nullptr};
    QPushButton* m_renameBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
};
