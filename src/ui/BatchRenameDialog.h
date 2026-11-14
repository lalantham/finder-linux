#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QStringList>

class QLineEdit;
class QCheckBox;
class QSpinBox;
class QListWidget;
class QLabel;

class BatchRenameDialog : public QDialog {
    Q_OBJECT

public:
    explicit BatchRenameDialog(const QStringList& paths, QWidget* parent = nullptr);

    [[nodiscard]] QList<QPair<QString, QString>> renames() const { return m_renames; }

private slots:
    void updatePreview();

private:
    QStringList m_paths;
    QList<QPair<QString, QString>> m_renames;

    QLineEdit* m_find{nullptr};
    QLineEdit* m_replace{nullptr};
    QLineEdit* m_prefix{nullptr};
    QLineEdit* m_suffix{nullptr};
    QCheckBox* m_number{nullptr};
    QSpinBox* m_start{nullptr};
    QSpinBox* m_padding{nullptr};
    QListWidget* m_preview{nullptr};
    QLabel* m_summary{nullptr};
};
