#include "CompressDialog.h"
#include "ArchiveService.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

CompressDialog::CompressDialog(const QStringList& sources, QWidget* parent)
    : QDialog(parent)
    , m_sources(sources)
{
    setWindowTitle(QStringLiteral("Compress"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();

    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(QStringLiteral("ZIP archive (.zip)"), QStringLiteral("zip"));
    m_formatCombo->addItem(QStringLiteral("7z archive (.7z)"), QStringLiteral("7z"));
    m_formatCombo->addItem(QStringLiteral("Tar archive (.tar)"), QStringLiteral("tar"));
    m_formatCombo->addItem(QStringLiteral("GZip tarball (.tar.gz)"), QStringLiteral("tar.gz"));
    m_formatCombo->addItem(QStringLiteral("XZ tarball (.tar.xz)"), QStringLiteral("tar.xz"));
    m_formatCombo->addItem(QStringLiteral("BZip2 tarball (.tar.bz2)"), QStringLiteral("tar.bz2"));
    m_formatCombo->addItem(QStringLiteral("Zstandard tarball (.tar.zst)"), QStringLiteral("tar.zst"));
    form->addRow(QStringLiteral("Format:"), m_formatCombo);

    m_pathEdit = new QLineEdit(this);
    form->addRow(QStringLiteral("Archive file:"), m_pathEdit);

    m_levelSpin = new QSpinBox(this);
    m_levelSpin->setRange(1, 9);
    m_levelSpin->setValue(6);
    m_levelSpin->setToolTip(QStringLiteral("1 = fastest, 9 = smallest"));
    form->addRow(QStringLiteral("Compression level:"), m_levelSpin);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("Optional (ZIP only)"));
    form->addRow(QStringLiteral("Password:"), m_passwordEdit);

    layout->addLayout(form);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(m_hintLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        const QString path = m_pathEdit->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Compress"),
                QStringLiteral("Choose a destination archive file."));
            return;
        }

        const QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath())) {
            QMessageBox::warning(this, QStringLiteral("Compress"),
                QStringLiteral("Cannot create the destination folder."));
            return;
        }

        if (QFile::exists(path)) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Compress"),
                QStringLiteral("\"%1\" already exists. Replace it?").arg(info.fileName()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }

        accept();
    });

    connect(m_formatCombo, &QComboBox::currentIndexChanged,
            this, &CompressDialog::updateFromFormat);

    updateFromFormat();
}

void CompressDialog::updateFromFormat()
{
    const QString extension = m_formatCombo->currentData().toString();
    m_pathEdit->setText(ArchiveService::suggestedArchiveName(m_sources, extension));

    const bool zip = extension == QStringLiteral("zip");
    const bool tarball = extension.startsWith(QStringLiteral("tar."));
    const bool noCompression = extension == QStringLiteral("tar");
    const bool knownLevel = zip || tarball;

    m_passwordEdit->setEnabled(zip);
    if (!zip) {
        m_passwordEdit->clear();
    }

    m_levelSpin->setEnabled(knownLevel);
    if (noCompression) {
        m_hintLabel->setText(QStringLiteral(
            "Tar archives are uncompressed. Choose a .tar.* format for compression."));
    } else if (extension == QStringLiteral("7z")) {
        m_hintLabel->setText(QStringLiteral(
            "Password protection is available for ZIP archives. "
            "The 7z writer in libarchive does not support custom levels or encryption."));
    } else if (zip) {
        m_hintLabel->setText(QStringLiteral(
            "ZIP supports optional AES password protection and compression levels 1-9."));
    } else {
        m_hintLabel->setText(QStringLiteral(
            "Compression levels apply to the selected tar filter."));
    }
}

QString CompressDialog::archivePath() const
{
    return m_pathEdit ? m_pathEdit->text().trimmed() : QString();
}

QString CompressDialog::passphrase() const
{
    return m_passwordEdit ? m_passwordEdit->text() : QString();
}

int CompressDialog::compressionLevel() const
{
    return m_levelSpin ? m_levelSpin->value() : -1;
}
