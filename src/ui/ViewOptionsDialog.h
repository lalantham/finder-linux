#pragma once

#include "AppSettings.h"
#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QSpinBox;

class ViewOptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ViewOptionsDialog(const QString& folderPath, QWidget* parent = nullptr);

    [[nodiscard]] FolderViewOptions options() const;
    [[nodiscard]] QStringList visibleColumns() const;

private:
    void loadFromSettings();
    void applyAsDefaults();

    QString m_folderPath;
    QSpinBox* m_iconSize{nullptr};
    QSpinBox* m_textSize{nullptr};
    QComboBox* m_labelPosition{nullptr};
    QSpinBox* m_gridSpacing{nullptr};
    QCheckBox* m_showItemInfo{nullptr};
    QCheckBox* m_showIconPreview{nullptr};
    QCheckBox* m_manualArrangement{nullptr};
    QMap<int, QCheckBox*> m_columnChecks;
};
