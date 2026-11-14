#include "TagManagementDialog.h"
#include "TagManager.h"
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TagManagementDialog::TagManagementDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Manage Tags"));
    setMinimumSize(440, 440);

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(
        QStringLiteral("Built-in tags cannot be renamed or deleted. Custom tags are stored "
                       "with your files using the standard user.xdg.tags attribute."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_list, 1);

    auto* buttonRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("Add Tag..."), this);
    m_renameBtn = new QPushButton(QStringLiteral("Rename..."), this);
    m_removeBtn = new QPushButton(QStringLiteral("Delete"), this);
    buttonRow->addWidget(addBtn);
    buttonRow->addWidget(m_renameBtn);
    buttonRow->addWidget(m_removeBtn);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto* bottom = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(bottom);

    connect(addBtn, &QPushButton::clicked, this, &TagManagementDialog::addTag);
    connect(m_renameBtn, &QPushButton::clicked, this, &TagManagementDialog::renameTag);
    connect(m_removeBtn, &QPushButton::clicked, this, &TagManagementDialog::removeTag);
    connect(bottom, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) {
        const bool custom = !isStandardTag(selectedTagName());
        m_renameBtn->setEnabled(custom);
        m_removeBtn->setEnabled(custom);
    });

    refresh();
}

void TagManagementDialog::refresh() {
    const QString selected = selectedTagName();
    m_list->clear();
    for (const TagInfo& tag : TagManager::allTags()) {
        const int count = TagManager::instance().tagCount(tag.name);
        auto* item = new QListWidgetItem(
            TagManager::tagDotIcon(tag.color, 16),
            count > 0 ? QStringLiteral("%1  (%2)").arg(tag.name).arg(count) : tag.name);
        item->setData(Qt::UserRole, tag.name);
        if (isStandardTag(tag.name)) {
            item->setToolTip(QStringLiteral("Built-in tag"));
        }
        m_list->addItem(item);
        if (tag.name == selected)
            m_list->setCurrentItem(item);
    }
}

QString TagManagementDialog::selectedTagName() const {
    QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool TagManagementDialog::isStandardTag(const QString& name) const {
    for (const TagInfo& tag : TagManager::standardTags()) {
        if (tag.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void TagManagementDialog::addTag() {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("Add Tag"), QStringLiteral("Tag name:"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;

    const QColor color =
        QColorDialog::getColor(QColor::fromHsv(static_cast<int>(qHash(name) % 360), 180, 230), this,
                               QStringLiteral("Tag Color"));
    if (!color.isValid())
        return;

    QString error;
    if (!TagManager::instance().addCustomTag(name, color, &error)) {
        QMessageBox::warning(this, QStringLiteral("Add Tag"), error);
        return;
    }
    refresh();
}

void TagManagementDialog::renameTag() {
    const QString oldName = selectedTagName();
    if (oldName.isEmpty() || isStandardTag(oldName))
        return;

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, QStringLiteral("Rename Tag"), QStringLiteral("New tag name:"),
                              QLineEdit::Normal, oldName, &ok)
            .trimmed();
    if (!ok || newName.isEmpty() || newName == oldName)
        return;

    QString error;
    if (!TagManager::instance().renameTag(oldName, newName, &error)) {
        QMessageBox::warning(this, QStringLiteral("Rename Tag"), error);
        return;
    }
    refresh();
}

void TagManagementDialog::removeTag() {
    const QString name = selectedTagName();
    if (name.isEmpty() || isStandardTag(name))
        return;

    if (QMessageBox::question(
            this, QStringLiteral("Delete Tag"),
            QStringLiteral("Delete the tag \"%1\" and remove it from all files?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!TagManager::instance().removeTagDefinition(name, &error)) {
        QMessageBox::warning(this, QStringLiteral("Delete Tag"), error);
        return;
    }
    refresh();
}
