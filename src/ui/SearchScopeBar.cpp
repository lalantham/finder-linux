#include "SearchScopeBar.h"
#include "StyleHelper.h"
#include <QIcon>

SearchScopeBar::SearchScopeBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(36);
    setupUi();
    updateThemeStyles(false);
}

void SearchScopeBar::setupUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setSpacing(8);

    m_labelSearch = new QLabel(QStringLiteral("Search:"), this);
    m_labelSearch->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: bold; color: #888888;"));
    layout->addWidget(m_labelSearch);

    // Segmented Scope Buttons
    auto* scopeRow = new QHBoxLayout();
    scopeRow->setSpacing(0);

    m_thisFolderBtn = new QPushButton(this);
    m_thisFolderBtn->setCheckable(true);
    m_thisFolderBtn->setChecked(true);
    m_thisFolderBtn->setFixedHeight(24);
    connect(m_thisFolderBtn, &QPushButton::clicked, this, &SearchScopeBar::onThisFolderClicked);
    scopeRow->addWidget(m_thisFolderBtn);

    m_everywhereBtn = new QPushButton(QStringLiteral("Everywhere"), this);
    m_everywhereBtn->setCheckable(true);
    m_everywhereBtn->setChecked(false);
    m_everywhereBtn->setFixedHeight(24);
    connect(m_everywhereBtn, &QPushButton::clicked, this, &SearchScopeBar::onEverywhereClicked);
    scopeRow->addWidget(m_everywhereBtn);

    layout->addLayout(scopeRow);

    // Subfolders toggle
    m_subfoldersCb = new QCheckBox(QStringLiteral("Subfolders"), this);
    m_subfoldersCb->setToolTip(QStringLiteral("Recursively search inside all subdirectories of this folder"));
    m_subfoldersCb->setStyleSheet(QStringLiteral("font-size: 11px;"));
    connect(m_subfoldersCb, &QCheckBox::toggled, this, &SearchScopeBar::subfoldersToggled);
    layout->addWidget(m_subfoldersCb);

    layout->addSpacing(8);

    // Kind filter
    m_kindLabel = new QLabel(QStringLiteral("Kind:"), this);
    m_kindLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888888;"));
    layout->addWidget(m_kindLabel);

    m_kindCombo = new QComboBox(this);
    m_kindCombo->setFixedHeight(24);
    m_kindCombo->addItem(QStringLiteral("Any Kind"), QString());
    m_kindCombo->addItem(QStringLiteral("Documents"), QStringLiteral("Documents"));
    m_kindCombo->addItem(QStringLiteral("Images"), QStringLiteral("Images"));
    m_kindCombo->addItem(QStringLiteral("Videos"), QStringLiteral("Videos"));
    m_kindCombo->addItem(QStringLiteral("Audio"), QStringLiteral("Audio"));
    m_kindCombo->addItem(QStringLiteral("Folders"), QStringLiteral("Folders"));
    m_kindCombo->addItem(QStringLiteral("Archives"), QStringLiteral("Archives"));
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        emit kindChanged(m_kindCombo->itemData(idx).toString());
    });
    layout->addWidget(m_kindCombo);

    layout->addSpacing(8);

    // Live status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888888; font-style: italic;"));
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    // Clear search button
    m_clearBtn = new QPushButton(QStringLiteral("✕ Clear"), this);
    m_clearBtn->setFixedHeight(24);
    m_clearBtn->setToolTip(QStringLiteral("Clear Search (Esc)"));
    connect(m_clearBtn, &QPushButton::clicked, this, &SearchScopeBar::clearRequested);
    layout->addWidget(m_clearBtn);

    setCurrentFolderName(QStringLiteral("This Folder"));
    updateScopeButtons();
}

void SearchScopeBar::setCurrentFolderName(const QString& name)
{
    m_folderName = name.isEmpty() ? QStringLiteral("This Folder") : name;
    m_thisFolderBtn->setText(QStringLiteral("\"%1\"").arg(m_folderName));
    int btnWidth = qMax(80, m_thisFolderBtn->fontMetrics().horizontalAdvance(m_thisFolderBtn->text()) + 20);
    m_thisFolderBtn->setFixedWidth(btnWidth);
}

void SearchScopeBar::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

void SearchScopeBar::reset()
{
    m_scope = SearchScope::ThisFolder;
    m_thisFolderBtn->setChecked(true);
    m_everywhereBtn->setChecked(false);
    m_subfoldersCb->blockSignals(true);
    m_subfoldersCb->setChecked(false);
    m_subfoldersCb->blockSignals(false);
    m_subfoldersCb->setVisible(true);
    m_kindCombo->blockSignals(true);
    m_kindCombo->setCurrentIndex(0);
    m_kindCombo->blockSignals(false);
    m_statusLabel->clear();
    updateScopeButtons();
}

QString SearchScopeBar::currentKind() const
{
    return m_kindCombo->currentData().toString();
}

bool SearchScopeBar::isSubfolders() const
{
    return m_subfoldersCb->isChecked();
}

void SearchScopeBar::onThisFolderClicked()
{
    if (m_scope != SearchScope::ThisFolder) {
        m_scope = SearchScope::ThisFolder;
        updateScopeButtons();
        m_subfoldersCb->setVisible(true);
        emit scopeChanged(m_scope);
    } else {
        m_thisFolderBtn->setChecked(true);
    }
}

void SearchScopeBar::onEverywhereClicked()
{
    if (m_scope != SearchScope::Everywhere) {
        m_scope = SearchScope::Everywhere;
        updateScopeButtons();
        m_subfoldersCb->setVisible(false);
        emit scopeChanged(m_scope);
    } else {
        m_everywhereBtn->setChecked(true);
    }
}

void SearchScopeBar::updateScopeButtons()
{
    m_thisFolderBtn->setChecked(m_scope == SearchScope::ThisFolder);
    m_everywhereBtn->setChecked(m_scope == SearchScope::Everywhere);
    updateThemeStyles(m_isDark);
}

void SearchScopeBar::updateThemeStyles(bool isDark)
{
    m_isDark = isDark;
    QString barBg = isDark ? QStringLiteral("#242428") : QStringLiteral("#ececf0");
    QString borderCol = isDark ? QStringLiteral("#1c1c1f") : QStringLiteral("#d5d5db");
    QString textCol = isDark ? QStringLiteral("#cccccc") : QStringLiteral("#333333");
    QString activeBg = isDark ? QStringLiteral("#3c3c44") : QStringLiteral("#ffffff");
    QString activeText = isDark ? QStringLiteral("#ffffff") : QStringLiteral("#111111");
    QString inactiveBg = isDark ? QStringLiteral("#2c2c32") : QStringLiteral("#dedee4");
    QString inactiveText = isDark ? QStringLiteral("#8e8e93") : QStringLiteral("#666666");

    setStyleSheet(QStringLiteral("SearchScopeBar { background-color: %1; border-bottom: 1px solid %2; }").arg(barBg, borderCol));

    QString leftBtnStyle = QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-right: none; border-top-left-radius: 4px; border-bottom-left-radius: 4px; padding: 2px 8px; font-size: 11px; font-weight: %4; }"
        "QPushButton:hover { background: %5; }"
    ).arg(
        m_scope == SearchScope::ThisFolder ? activeBg : inactiveBg,
        m_scope == SearchScope::ThisFolder ? activeText : inactiveText,
        borderCol,
        m_scope == SearchScope::ThisFolder ? QStringLiteral("bold") : QStringLiteral("normal"),
        m_scope == SearchScope::ThisFolder ? activeBg : (isDark ? QStringLiteral("#34343a") : QStringLiteral("#e6e6ec"))
    );
    m_thisFolderBtn->setStyleSheet(leftBtnStyle);

    QString rightBtnStyle = QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-top-right-radius: 4px; border-bottom-right-radius: 4px; padding: 2px 8px; font-size: 11px; font-weight: %4; }"
        "QPushButton:hover { background: %5; }"
    ).arg(
        m_scope == SearchScope::Everywhere ? activeBg : inactiveBg,
        m_scope == SearchScope::Everywhere ? activeText : inactiveText,
        borderCol,
        m_scope == SearchScope::Everywhere ? QStringLiteral("bold") : QStringLiteral("normal"),
        m_scope == SearchScope::Everywhere ? activeBg : (isDark ? QStringLiteral("#34343a") : QStringLiteral("#e6e6ec"))
    );
    m_everywhereBtn->setStyleSheet(rightBtnStyle);

    m_kindCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 6px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; width: 14px; }"
        "QComboBox QAbstractItemView { background: %1; color: %2; selection-background-color: #0A84FF; }"
    ).arg(inactiveBg, textCol, borderCol));

    m_clearBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 10px; font-size: 11px; }"
        "QPushButton:hover { background: %4; color: #ff5555; }"
    ).arg(inactiveBg, textCol, borderCol, isDark ? QStringLiteral("#36363c") : QStringLiteral("#e2e2e8")));
}
