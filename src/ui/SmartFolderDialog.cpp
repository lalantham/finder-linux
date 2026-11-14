#include "SmartFolderDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

SmartFolderDialog::SmartFolderDialog(const SmartFolder& folder, const SearchFilter& initialFilter,
                                     QWidget* parent)
    : QDialog(parent) {
    const bool editing = folder.isValid();
    const SearchFilter filter = editing ? folder.filter : initialFilter;
    m_originalName = editing ? folder.name : QString();

    setWindowTitle(editing ? QStringLiteral("Edit Smart Folder")
                           : QStringLiteral("New Smart Folder"));
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);

    auto* intro =
        new QLabel(QStringLiteral("Smart Folders are saved searches. Use search tokens such as "
                                  "<b>kind:image</b>, <b>tag:Red</b>, <b>size:&gt;10MB</b>, "
                                  "<b>modified:&gt;2025-01-01</b>, or <b>content:invoice</b>."),
                   this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(editing ? folder.name : QString(), this);
    m_nameEdit->setPlaceholderText(QStringLiteral("e.g. Large recent images"));
    form->addRow(QStringLiteral("Name:"), m_nameEdit);

    m_queryEdit = new QLineEdit(filter.query, this);
    m_queryEdit->setPlaceholderText(QStringLiteral("Search query"));
    form->addRow(QStringLiteral("Query:"), m_queryEdit);

    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(QStringLiteral("This PC"));
    m_scopeCombo->addItem(QStringLiteral("Folder"));
    m_scopeCombo->setCurrentIndex(filter.thisPC ? 0 : 1);
    form->addRow(QStringLiteral("Search in:"), m_scopeCombo);

    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(6);
    m_pathEdit =
        new QLineEdit(filter.searchPath.isEmpty() ? QDir::homePath() : filter.searchPath, pathRow);
    m_browseBtn = new QPushButton(QStringLiteral("Browse..."), pathRow);
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(m_browseBtn);
    form->addRow(QStringLiteral("Folder:"), pathRow);

    m_recursiveCheck = new QCheckBox(QStringLiteral("Include subfolders"), this);
    m_recursiveCheck->setChecked(filter.recursive);
    form->addRow(QString(), m_recursiveCheck);

    m_contentsCheck = new QCheckBox(QStringLiteral("Search inside file contents"), this);
    m_contentsCheck->setChecked(filter.searchContents);
    form->addRow(QString(), m_contentsCheck);

    layout->addLayout(form);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(m_previewLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #e05b4f; font-size: 11px;"));
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SmartFolderDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_browseBtn, &QPushButton::clicked, this, &SmartFolderDialog::browseForFolder);
    connect(m_scopeCombo, &QComboBox::currentIndexChanged, this, &SmartFolderDialog::updateUi);
    connect(m_queryEdit, &QLineEdit::textChanged, this, &SmartFolderDialog::updatePreview);

    updateUi();
    updatePreview();
}

void SmartFolderDialog::browseForFolder() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Search Folder"), m_pathEdit->text());
    if (!folder.isEmpty()) {
        m_pathEdit->setText(folder);
    }
}

void SmartFolderDialog::updateUi() {
    const bool folderScope = m_scopeCombo->currentIndex() == 1;
    m_pathEdit->setEnabled(folderScope);
    m_browseBtn->setEnabled(folderScope);
    m_recursiveCheck->setEnabled(folderScope);
}

void SmartFolderDialog::updatePreview() {
    const SearchFilter filter = SearchQuery::parse(m_queryEdit->text());
    const QString summary = SearchQuery::summary(filter);
    if (summary.isEmpty()) {
        m_previewLabel->setText(QStringLiteral("No filters yet — type a search query above."));
    } else {
        m_previewLabel->setText(QStringLiteral("Filters: %1").arg(summary));
    }
}

void SmartFolderDialog::accept() {
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("Please enter a name for the Smart Folder."));
        m_errorLabel->show();
        return;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        m_errorLabel->setText(QStringLiteral("Smart Folder names cannot contain '/' or '\\'."));
        m_errorLabel->show();
        return;
    }

    SearchFilter filter = SearchQuery::parse(m_queryEdit->text().trimmed());
    filter.query = m_queryEdit->text().trimmed();
    filter.thisPC = m_scopeCombo->currentIndex() == 0;
    filter.recursive = filter.thisPC ? true : m_recursiveCheck->isChecked();
    filter.searchContents = filter.searchContents || m_contentsCheck->isChecked();
    if (filter.searchContents && filter.contentTerms.isEmpty()) {
        filter.contentTerms = filter.nameTerms;
    }
    if (filter.thisPC) {
        filter.searchPath.clear();
        filter.searchPaths.clear();
    } else {
        const QString path = QDir::cleanPath(m_pathEdit->text().trimmed());
        if (path.isEmpty() || !QDir(path).exists()) {
            m_errorLabel->setText(QStringLiteral("Choose an existing folder to search."));
            m_errorLabel->show();
            return;
        }
        filter.searchPath = path;
    }

    m_result.name = name;
    m_result.filter = filter;
    QDialog::accept();
}
