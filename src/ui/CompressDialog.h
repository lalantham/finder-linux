#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;

class CompressDialog : public QDialog {
    Q_OBJECT

public:
    explicit CompressDialog(const QStringList& sources, QWidget* parent = nullptr);

    [[nodiscard]] QString archivePath() const;
    [[nodiscard]] QString passphrase() const;
    [[nodiscard]] int compressionLevel() const;

private:
    void updateFromFormat();

    QStringList m_sources;
    QComboBox* m_formatCombo{nullptr};
    QLineEdit* m_pathEdit{nullptr};
    QSpinBox* m_levelSpin{nullptr};
    QLineEdit* m_passwordEdit{nullptr};
    QLabel* m_hintLabel{nullptr};
};
