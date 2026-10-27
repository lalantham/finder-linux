#include "AppChooserDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

AppChooserDialog::AppChooserDialog(const QString& mimeType, const QString& fileName, QWidget* parent)
    : QDialog(parent)
    , m_mimeType(mimeType)
{
    setWindowTitle(QStringLiteral("Choose Application"));
    resize(440, 480);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(16, 16, 16, 16);

    auto* titleLabel = new QLabel(QStringLiteral("Choose an application to open <b>%1</b>:").arg(fileName), this);
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);

    m_searchFilter = new QLineEdit(this);
    m_searchFilter->setPlaceholderText(QStringLiteral("Filter applications..."));
    m_searchFilter->setClearButtonEnabled(true);
    connect(m_searchFilter, &QLineEdit::textChanged, this, &AppChooserDialog::onSearchFilterChanged);
    layout->addWidget(m_searchFilter);

    m_appList = new QListWidget(this);
    m_appList->setIconSize(QSize(28, 28));
    connect(m_appList, &QListWidget::itemDoubleClicked, this, &AppChooserDialog::onItemDoubleClicked);
    layout->addWidget(m_appList);

    m_alwaysUseBox = new QCheckBox(QStringLiteral("Always open with this application"), this);
    layout->addWidget(m_alwaysUseBox);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_openBtn = new QPushButton(QStringLiteral("Open"), this);
    m_openBtn->setDefault(true);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_openBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_openBtn);
    layout->addLayout(btnRow);

    populateApps();
}

void AppChooserDialog::populateApps()
{
    m_apps = DesktopAppsManager::allDesktopApps();
    onSearchFilterChanged(QString());
}

void AppChooserDialog::onSearchFilterChanged(const QString& query)
{
    m_appList->clear();
    QString trimmed = query.trimmed();

    for (const auto& app : m_apps) {
        if (!trimmed.isEmpty() && !app.name.contains(trimmed, Qt::CaseInsensitive)) {
            continue;
        }

        auto* item = new QListWidgetItem(app.icon, app.name, m_appList);
        item->setData(Qt::UserRole, app.id);
    }

    if (m_appList->count() > 0) {
        m_appList->setCurrentRow(0);
    }
}

void AppChooserDialog::onItemDoubleClicked(QListWidgetItem* /*item*/)
{
    accept();
}

QString AppChooserDialog::selectedAppId() const
{
    QListWidgetItem* item = m_appList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).toString();
    }
    return {};
}

bool AppChooserDialog::makeDefault() const
{
    return m_alwaysUseBox && m_alwaysUseBox->isChecked();
}
