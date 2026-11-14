#pragma once

#include "SearchQuery.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class SmartFolderDialog : public QDialog {
    Q_OBJECT

public:
    explicit SmartFolderDialog(const SmartFolder& folder = {},
                               const SearchFilter& initialFilter = {}, QWidget* parent = nullptr);

    [[nodiscard]] SmartFolder smartFolder() const {
        return m_result;
    }
    [[nodiscard]] QString originalName() const {
        return m_originalName;
    }

protected:
    void accept() override;

private slots:
    void browseForFolder();
    void updateUi();
    void updatePreview();

private:
    QString m_originalName;
    SmartFolder m_result;

    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_queryEdit{nullptr};
    QComboBox* m_scopeCombo{nullptr};
    QLineEdit* m_pathEdit{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QCheckBox* m_recursiveCheck{nullptr};
    QCheckBox* m_contentsCheck{nullptr};
    QLabel* m_previewLabel{nullptr};
    QLabel* m_errorLabel{nullptr};
};
