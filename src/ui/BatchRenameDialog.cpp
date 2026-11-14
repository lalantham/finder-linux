#include "BatchRenameDialog.h"
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

BatchRenameDialog::BatchRenameDialog(const QStringList& paths, QWidget* parent)
    : QDialog(parent)
    , m_paths(paths)
{
    setWindowTitle(QStringLiteral("Batch Rename (%1 items)").arg(paths.size()));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);

    auto* findRow = new QFormLayout();
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(QStringLiteral("Text to replace"));
    m_replace = new QLineEdit(this);
    m_replace->setPlaceholderText(QStringLiteral("Replacement (leave empty to remove)"));
    findRow->addRow(QStringLiteral("Find:"), m_find);
    findRow->addRow(QStringLiteral("Replace with:"), m_replace);
    m_prefix = new QLineEdit(this);
    m_prefix->setPlaceholderText(QStringLiteral("Added before the name"));
    m_suffix = new QLineEdit(this);
    m_suffix->setPlaceholderText(QStringLiteral("Added after the name"));
    findRow->addRow(QStringLiteral("Prefix:"), m_prefix);
    findRow->addRow(QStringLiteral("Suffix:"), m_suffix);
    layout->addLayout(findRow);

    auto* numberRow = new QHBoxLayout();
    m_number = new QCheckBox(QStringLiteral("Add number"), this);
    m_number->setChecked(false);
    auto* startLabel = new QLabel(QStringLiteral("Start at:"), this);
    m_start = new QSpinBox(this);
    m_start->setRange(0, 999999);
    m_start->setValue(1);
    auto* paddingLabel = new QLabel(QStringLiteral("Digits:"), this);
    m_padding = new QSpinBox(this);
    m_padding->setRange(1, 6);
    m_padding->setValue(2);
    numberRow->addWidget(m_number);
    numberRow->addWidget(startLabel);
    numberRow->addWidget(m_start);
    numberRow->addWidget(paddingLabel);
    numberRow->addWidget(m_padding);
    numberRow->addStretch();
    layout->addLayout(numberRow);

    auto* previewLabel = new QLabel(QStringLiteral("Preview:"), this);
    layout->addWidget(previewLabel);
    m_preview = new QListWidget(this);
    m_preview->setMinimumHeight(180);
    layout->addWidget(m_preview);

    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color: #888888;"));
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    for (QLineEdit* edit : {m_find, m_replace, m_prefix, m_suffix}) {
        connect(edit, &QLineEdit::textChanged, this, &BatchRenameDialog::updatePreview);
    }
    connect(m_number, &QCheckBox::toggled, this, &BatchRenameDialog::updatePreview);
    connect(m_start, &QSpinBox::valueChanged, this, &BatchRenameDialog::updatePreview);
    connect(m_padding, &QSpinBox::valueChanged, this, &BatchRenameDialog::updatePreview);

    updatePreview();
}

void BatchRenameDialog::updatePreview()
{
    m_preview->clear();
    m_renames.clear();

    const QString find = m_find->text();
    const QString replace = m_replace->text();
    const QString prefix = m_prefix->text();
    const QString suffix = m_suffix->text();
    const bool numbering = m_number->isChecked();
    const int start = m_start->value();
    const int padding = m_padding->value();

    QSet<QString> seenTargets;
    int conflicts = 0;
    int changed = 0;

    for (int i = 0; i < m_paths.size(); ++i) {
        const QString& path = m_paths.at(i);
        QFileInfo info(path);
        const QString extension = info.suffix();
        QString base = info.completeBaseName();

        if (!find.isEmpty()) {
            base.replace(find, replace);
        }

        QString newName = prefix + base + suffix;
        if (numbering) {
            newName += QLatin1Char(' ') + QString::number(start + i).rightJustified(padding, QLatin1Char('0'));
        }
        if (!extension.isEmpty()) {
            newName += QLatin1Char('.') + extension;
        }

        QString reason;
        if (newName.trimmed().isEmpty()) {
            reason = QStringLiteral("empty name");
        } else if (newName.contains(QLatin1Char('/'))) {
            reason = QStringLiteral("contains '/'");
        } else if (newName == QStringLiteral(".") || newName == QStringLiteral("..")) {
            reason = QStringLiteral("invalid name");
        }

        const QString target = info.absolutePath() + QLatin1Char('/') + newName;
        if (reason.isEmpty() && target != path) {
            if (QFile::exists(target) || QFileInfo(target).isSymLink()) {
                reason = QStringLiteral("already exists");
            } else if (seenTargets.contains(target)) {
                reason = QStringLiteral("duplicate name");
            }
        }

        if (!reason.isEmpty()) {
            conflicts++;
            auto* row = new QListWidgetItem(
                QStringLiteral("%1  →  %2   [%3]").arg(info.fileName(), newName, reason));
            row->setForeground(QColor(0xe0, 0x5b, 0x4f));
            m_preview->addItem(row);
            continue;
        }

        if (target != path) {
            changed++;
            m_renames.append(qMakePair(path, target));
        }
        seenTargets.insert(target);
        m_preview->addItem(QStringLiteral("%1  →  %2").arg(info.fileName(), newName));
    }

    if (conflicts > 0) {
        m_summary->setText(QStringLiteral("%1 item(s) cannot be renamed and will be skipped.").arg(conflicts));
    } else if (changed == 0) {
        m_summary->setText(QStringLiteral("No changes."));
    } else {
        m_summary->setText(QStringLiteral("%1 of %2 item(s) will be renamed.").arg(changed).arg(m_paths.size()));
    }
}
