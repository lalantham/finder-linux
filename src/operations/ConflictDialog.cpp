#include "ConflictDialog.h"
#include "FileItem.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileInfo>
#include <QFileIconProvider>

ConflictDialog::ConflictDialog(const OperationConflict& conflict, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("File Conflict"));
    setFixedWidth(460);
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QFileInfo fi(conflict.destinationPath);
    auto* titleLabel = new QLabel(
        QStringLiteral("<b>An item named \"%1\" already exists in this location.</b><br>"
                       "Do you want to replace it with the newer one?").arg(fi.fileName()), this);
    titleLabel->setWordWrap(true);
    mainLayout->addWidget(titleLabel);

    // Grid comparing Existing vs Source item
    auto* grid = new QGridLayout();
    grid->setSpacing(10);

    QFileIconProvider iconProv;
    QIcon icon = iconProv.icon(fi);
    auto* iconLabel = new QLabel(this);
    iconLabel->setPixmap(icon.pixmap(48, 48));
    grid->addWidget(iconLabel, 0, 0, 2, 1);

    auto* existLabel = new QLabel(
        QStringLiteral("<b>Existing item:</b><br>Size: %1<br>Modified: %2")
            .arg(FileItem::formatSize(conflict.destinationSize),
                 conflict.destinationModified.toString(QStringLiteral("yyyy-MM-dd hh:mm"))), this);
    grid->addWidget(existLabel, 0, 1);

    auto* srcLabel = new QLabel(
        QStringLiteral("<b>New item:</b><br>Size: %1<br>Modified: %2")
            .arg(FileItem::formatSize(conflict.sourceSize),
                 conflict.sourceModified.toString(QStringLiteral("yyyy-MM-dd hh:mm"))), this);
    grid->addWidget(srcLabel, 1, 1);

    mainLayout->addLayout(grid);

    m_applyAllBox = new QCheckBox(QStringLiteral("Apply to all"), this);
    mainLayout->addWidget(m_applyAllBox);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    auto* btnCancel = new QPushButton(QStringLiteral("Cancel"), this);
    auto* btnSkip = new QPushButton(QStringLiteral("Skip"), this);
    auto* btnKeepBoth = new QPushButton(QStringLiteral("Keep Both"), this);
    auto* btnReplace = new QPushButton(QStringLiteral("Replace"), this);
    btnReplace->setDefault(true);

    btnLayout->addWidget(btnCancel);
    btnLayout->addStretch();
    btnLayout->addWidget(btnSkip);
    btnLayout->addWidget(btnKeepBoth);
    btnLayout->addWidget(btnReplace);
    mainLayout->addLayout(btnLayout);

    connect(btnCancel, &QPushButton::clicked, this, &ConflictDialog::onCancelClicked);
    connect(btnSkip, &QPushButton::clicked, this, &ConflictDialog::onSkipClicked);
    connect(btnKeepBoth, &QPushButton::clicked, this, &ConflictDialog::onKeepBothClicked);
    connect(btnReplace, &QPushButton::clicked, this, &ConflictDialog::onReplaceClicked);
}

void ConflictDialog::onReplaceClicked()
{
    m_resolution = ConflictResolution::Replace;
    m_applyToAll = m_applyAllBox->isChecked();
    accept();
}

void ConflictDialog::onKeepBothClicked()
{
    m_resolution = ConflictResolution::KeepBoth;
    m_applyToAll = m_applyAllBox->isChecked();
    accept();
}

void ConflictDialog::onSkipClicked()
{
    m_resolution = ConflictResolution::Skip;
    m_applyToAll = m_applyAllBox->isChecked();
    accept();
}

void ConflictDialog::onCancelClicked()
{
    m_resolution = ConflictResolution::Cancel;
    m_applyToAll = false;
    reject();
}
