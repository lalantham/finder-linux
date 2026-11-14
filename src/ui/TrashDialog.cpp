#include "TrashDialog.h"
#include "FileItem.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

TrashDialog::TrashDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Trash"));
    resize(760, 440);

    auto* layout = new QVBoxLayout(this);

    m_tree = new QTreeWidget(this);
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Original Location"),
                             QStringLiteral("Deleted"), QStringLiteral("Size")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree);

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    auto* buttons = new QHBoxLayout();
    m_restoreBtn = new QPushButton(QStringLiteral("Restore"), this);
    m_deleteBtn = new QPushButton(QStringLiteral("Delete Immediately"), this);
    m_emptyBtn = new QPushButton(QStringLiteral("Empty Trash"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    buttons->addWidget(m_restoreBtn);
    buttons->addWidget(m_deleteBtn);
    buttons->addStretch();
    buttons->addWidget(m_emptyBtn);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(m_restoreBtn, &QPushButton::clicked, this, &TrashDialog::restoreSelected);
    connect(m_deleteBtn, &QPushButton::clicked, this, &TrashDialog::deleteSelected);
    connect(m_emptyBtn, &QPushButton::clicked, this, &TrashDialog::emptyAll);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &TrashDialog::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this]() { restoreSelected(); });

    reload();
}

void TrashDialog::reload()
{
    m_tree->clear();
    m_items = XdgTrash::listTrashItems();

    std::sort(m_items.begin(), m_items.end(), [](const TrashItem& a, const TrashItem& b) {
        return a.deletionDate > b.deletionDate;
    });

    for (int i = 0; i < m_items.size(); ++i) {
        const TrashItem& item = m_items.at(i);
        auto* row = new QTreeWidgetItem(m_tree);
        row->setText(0, QFileInfo(item.originalPath).fileName());
        row->setText(1, item.originalPath);
        row->setText(2, item.deletionDate.isValid()
                            ? item.deletionDate.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                            : QStringLiteral("unknown"));
        row->setText(3, item.isDir ? QStringLiteral("--") : FileItem::formatSize(item.size));
        row->setData(0, Qt::UserRole, i);
    }

    m_summary->setText(QStringLiteral("%1 item%2 in Trash")
                           .arg(m_items.size())
                           .arg(m_items.size() == 1 ? QString() : QStringLiteral("s")));
    updateButtons();
}

void TrashDialog::updateButtons()
{
    const bool hasSelection = !m_tree->selectedItems().isEmpty();
    if (m_restoreBtn) m_restoreBtn->setEnabled(hasSelection);
    if (m_deleteBtn) m_deleteBtn->setEnabled(hasSelection);
    if (m_emptyBtn) m_emptyBtn->setEnabled(!m_items.isEmpty());
}

void TrashDialog::restoreSelected()
{
    const auto selected = m_tree->selectedItems();
    if (selected.isEmpty()) return;

    int failed = 0;
    QString lastError;
    QStringList restoredWithNewNames;
    for (auto* row : selected) {
        const int index = row->data(0, Qt::UserRole).toInt();
        if (index < 0 || index >= m_items.size()) continue;
        const TrashItem& item = m_items.at(index);
        QString error;
        QString restoredPath;
        if (!XdgTrash::restoreItem(item.trashFilePath, &error, &restoredPath)) {
            failed++;
            lastError = error;
        } else if (restoredPath != item.originalPath) {
            restoredWithNewNames.append(QFileInfo(restoredPath).fileName());
        }
    }

    reload();
    emit trashChanged();

    if (failed > 0) {
        QMessageBox::warning(this, QStringLiteral("Restore Failed"),
            failed == 1 ? lastError : QStringLiteral("%1 items could not be restored: %2").arg(failed).arg(lastError));
    }
    if (!restoredWithNewNames.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Restored with New Names"),
            QStringLiteral("These items already existed at their original locations, "
                           "so they were restored with new names:\n\n%1")
                .arg(restoredWithNewNames.join(QLatin1Char('\n'))));
    }
}

void TrashDialog::deleteSelected()
{
    const auto selected = m_tree->selectedItems();
    if (selected.isEmpty()) return;

    const auto answer = QMessageBox::warning(this, QStringLiteral("Delete Permanently"),
        QStringLiteral("Permanently delete the selected %1 item(s)?\nThis cannot be undone.").arg(selected.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    int failed = 0;
    for (auto* row : selected) {
        const int index = row->data(0, Qt::UserRole).toInt();
        if (index < 0 || index >= m_items.size()) continue;
        const TrashItem item = m_items.at(index);

        bool ok = false;
        QFileInfo info(item.trashFilePath);
        if (info.isDir() && !info.isSymLink()) {
            ok = QDir(item.trashFilePath).removeRecursively();
        } else {
            ok = QFile::remove(item.trashFilePath);
        }
        if (ok) {
            QFile::remove(item.trashInfoPath);
        } else {
            failed++;
        }
    }

    reload();
    emit trashChanged();

    if (failed > 0) {
        QMessageBox::warning(this, QStringLiteral("Delete Failed"),
            QStringLiteral("%1 item(s) could not be deleted.").arg(failed));
    }
}

void TrashDialog::emptyAll()
{
    if (m_items.isEmpty()) return;

    const auto answer = QMessageBox::warning(this, QStringLiteral("Empty Trash"),
        QStringLiteral("Permanently erase all items in the Trash?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QString error;
    if (!XdgTrash::emptyTrash(&error)) {
        QMessageBox::warning(this, QStringLiteral("Empty Trash"), error);
    }

    reload();
    emit trashChanged();
}
