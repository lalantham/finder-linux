#pragma once

#include "FileOperation.h"
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QPushButton>

class ConflictDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConflictDialog(const OperationConflict& conflict, QWidget* parent = nullptr);

    [[nodiscard]] ConflictResolution resolution() const noexcept {
        return m_resolution;
    }
    [[nodiscard]] bool applyToAll() const noexcept {
        return m_applyToAll;
    }

private slots:
    void onMergeClicked();
    void onReplaceClicked();
    void onKeepBothClicked();
    void onSkipClicked();
    void onCancelClicked();

private:
    ConflictResolution m_resolution{ConflictResolution::Cancel};
    bool m_applyToAll{false};
    QCheckBox* m_applyAllBox{nullptr};
};
