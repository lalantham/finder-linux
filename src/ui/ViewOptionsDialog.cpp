#include "ViewOptionsDialog.h"
#include "FileSystemModel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ViewOptionsDialog::ViewOptionsDialog(const QString& folderPath, QWidget* parent)
    : QDialog(parent), m_folderPath(folderPath) {
    setWindowTitle(QStringLiteral("View Options"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        QStringLiteral("These settings apply to the current folder. "
                       "Use “Use as Defaults” to apply them to folders without their own options."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    m_iconSize = new QSpinBox(this);
    m_iconSize->setRange(16, 256);
    m_iconSize->setSingleStep(8);
    m_iconSize->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("Icon size:"), m_iconSize);

    m_textSize = new QSpinBox(this);
    m_textSize->setRange(8, 24);
    m_textSize->setSuffix(QStringLiteral(" pt"));
    form->addRow(QStringLiteral("Text size:"), m_textSize);

    m_labelPosition = new QComboBox(this);
    m_labelPosition->addItem(QStringLiteral("Below icon"));
    m_labelPosition->addItem(QStringLiteral("To the right of icon"));
    form->addRow(QStringLiteral("Label position:"), m_labelPosition);

    m_gridSpacing = new QSpinBox(this);
    m_gridSpacing->setRange(0, 48);
    m_gridSpacing->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("Grid spacing:"), m_gridSpacing);

    m_showItemInfo = new QCheckBox(QStringLiteral("Show item info (size and item count)"), this);
    form->addRow(QString(), m_showItemInfo);

    m_showIconPreview = new QCheckBox(QStringLiteral("Show icon preview (thumbnails)"), this);
    form->addRow(QString(), m_showIconPreview);

    m_manualArrangement = new QCheckBox(QStringLiteral("Arrange icons manually"), this);
    m_manualArrangement->setToolTip(
        QStringLiteral("When enabled, drag icons to place them where you want. "
                       "Clean Up or Align to Grid restores the automatic layout."));
    form->addRow(QString(), m_manualArrangement);
    layout->addLayout(form);

    auto* columnsBox = new QGroupBox(QStringLiteral("List View Columns"), this);
    auto* columnsGrid = new QGridLayout(columnsBox);
    int row = 0;
    int column = 0;
    for (const int columnIndex : FileSystemModel::allColumns()) {
        auto* check = new QCheckBox(FileSystemModel::columnTitle(columnIndex), columnsBox);
        if (columnIndex == FileSystemModel::ColName) {
            check->setChecked(true);
            check->setEnabled(false);
            check->setToolTip(QStringLiteral("The Name column is always visible."));
        }
        columnsGrid->addWidget(check, row, column);
        m_columnChecks.insert(columnIndex, check);
        ++column;
        if (column == 2) {
            column = 0;
            ++row;
        }
    }
    layout->addWidget(columnsBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* defaultsButton = new QPushButton(QStringLiteral("Use as Defaults"), this);
    buttons->addButton(defaultsButton, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(defaultsButton, &QPushButton::clicked, this, &ViewOptionsDialog::applyAsDefaults);

    loadFromSettings();
}

void ViewOptionsDialog::loadFromSettings() {
    const FolderViewOptions options = AppSettings::instance().viewOptions(m_folderPath);
    m_iconSize->setValue(options.iconSize);
    m_textSize->setValue(options.textSize);
    m_labelPosition->setCurrentIndex(options.labelPosition == 1 ? 1 : 0);
    m_gridSpacing->setValue(options.gridSpacing);
    m_showItemInfo->setChecked(options.showItemInfo);
    m_showIconPreview->setChecked(options.showIconPreview);
    m_manualArrangement->setChecked(options.manualArrangement);

    const QStringList visible = AppSettings::instance().listVisibleColumns();
    for (auto it = m_columnChecks.cbegin(); it != m_columnChecks.cend(); ++it) {
        const QString key = FileSystemModel::columnKey(it.key());
        it.value()->setChecked(it.key() == FileSystemModel::ColName || visible.contains(key));
    }
}

FolderViewOptions ViewOptionsDialog::options() const {
    FolderViewOptions options;
    options.iconSize = m_iconSize->value();
    options.textSize = m_textSize->value();
    options.labelPosition = m_labelPosition->currentIndex() == 1 ? 1 : 0;
    options.gridSpacing = m_gridSpacing->value();
    options.showItemInfo = m_showItemInfo->isChecked();
    options.showIconPreview = m_showIconPreview->isChecked();
    options.manualArrangement = m_manualArrangement->isChecked();
    return options;
}

QStringList ViewOptionsDialog::visibleColumns() const {
    QStringList columns;
    for (auto it = m_columnChecks.cbegin(); it != m_columnChecks.cend(); ++it) {
        if (it.value()->isChecked()) {
            columns.append(FileSystemModel::columnKey(it.key()));
        }
    }
    return columns;
}

void ViewOptionsDialog::applyAsDefaults() {
    AppSettings::instance().setDefaultViewOptions(options());
    AppSettings::instance().setListVisibleColumns(visibleColumns());
}
