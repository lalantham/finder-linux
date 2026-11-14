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

    auto* customRow = new QHBoxLayout();
    auto* customLabel = new QLabel(QStringLiteral("Custom command:"), this);
    m_customCommandEdit = new QLineEdit(this);
    m_customCommandEdit->setPlaceholderText(QStringLiteral("e.g. mpv --fullscreen %F"));
    m_customCommandEdit->setClearButtonEnabled(true);
    connect(m_customCommandEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
    customRow->addWidget(customLabel);
    customRow->addWidget(m_customCommandEdit);
    layout->addLayout(customRow);

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
    m_recommendedApps = DesktopAppsManager::appsForMime(m_mimeType);
    QString defaultId = DesktopAppsManager::defaultAppIdForMime(m_mimeType);
    for (auto& app : m_recommendedApps) {
        if (app.id == defaultId) {
            app.isDefault = true;
        }
    }

    QSet<QString> recIds;
    for (const auto& app : m_recommendedApps) {
        recIds.insert(app.id);
    }

    m_otherApps.clear();
    const QVector<AppEntry> all = DesktopAppsManager::allDesktopApps();
    for (const auto& app : all) {
        if (!recIds.contains(app.id)) {
            m_otherApps.append(app);
        }
    }

    onSearchFilterChanged(QString());
}

void AppChooserDialog::onSearchFilterChanged(const QString& query)
{
    m_appList->clear();
    QString trimmed = query.trimmed();

    auto addSection = [this, &trimmed](const QString& title, const QVector<AppEntry>& list) {
        bool headerAdded = false;
        for (const auto& app : list) {
            if (!trimmed.isEmpty() && !app.name.contains(trimmed, Qt::CaseInsensitive) && !app.exec.contains(trimmed, Qt::CaseInsensitive)) {
                continue;
            }
            if (!headerAdded) {
                auto* header = new QListWidgetItem(title, m_appList);
                header->setFlags(Qt::NoItemFlags);
                QFont f = header->font();
                f.setBold(true);
                f.setPointSize(8);
                header->setFont(f);
                header->setForeground(QBrush(QColor(130, 130, 130)));
                headerAdded = true;
            }
            QString label = app.isDefault ? QStringLiteral("%1 (Default)").arg(app.name) : app.name;
            auto* item = new QListWidgetItem(app.icon, label, m_appList);
            item->setData(Qt::UserRole, app.id);
        }
    };

    if (!m_recommendedApps.isEmpty()) {
        addSection(QStringLiteral("RECOMMENDED APPLICATIONS"), m_recommendedApps);
    }
    addSection(QStringLiteral("OTHER APPLICATIONS"), m_otherApps);

    for (int i = 0; i < m_appList->count(); ++i) {
        if (m_appList->item(i)->flags() & Qt::ItemIsSelectable) {
            m_appList->setCurrentRow(i);
            break;
        }
    }
}

void AppChooserDialog::onItemDoubleClicked(QListWidgetItem* item)
{
    if (item && (item->flags() & Qt::ItemIsSelectable)) {
        accept();
    }
}

QString AppChooserDialog::selectedAppId() const
{
    QListWidgetItem* item = m_appList->currentItem();
    if (item && (item->flags() & Qt::ItemIsSelectable)) {
        return item->data(Qt::UserRole).toString();
    }
    return {};
}

QString AppChooserDialog::customCommand() const
{
    return m_customCommandEdit ? m_customCommandEdit->text().trimmed() : QString();
}

bool AppChooserDialog::makeDefault() const
{
    return m_alwaysUseBox && m_alwaysUseBox->isChecked();
}
