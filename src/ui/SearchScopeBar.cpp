#include "SearchScopeBar.h"
#include "StyleHelper.h"
#include "ThemeManager.h"
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>

SearchScopeBar::SearchScopeBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(36);
    setupUi();
    updateThemeStyles(false);
}

void SearchScopeBar::setupUi() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setSpacing(8);

    m_labelSearch = new QLabel(QStringLiteral("Search:"), this);
    m_labelSearch->setStyleSheet(
        QStringLiteral("font-size: 11px; font-weight: bold; color: #888888;"));
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

    m_thisPCBtn = new QPushButton(QStringLiteral("This PC"), this);
    m_thisPCBtn->setCheckable(true);
    m_thisPCBtn->setChecked(false);
    m_thisPCBtn->setFixedHeight(24);
    connect(m_thisPCBtn, &QPushButton::clicked, this, &SearchScopeBar::onThisPCClicked);
    scopeRow->addWidget(m_thisPCBtn);

    layout->addLayout(scopeRow);

    // Subfolders toggle
    m_subfoldersCb = new QCheckBox(QStringLiteral("Subfolders"), this);
    m_subfoldersCb->setToolTip(
        QStringLiteral("Recursively search inside all subdirectories of this folder"));
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
    connect(m_kindCombo, &QComboBox::currentIndexChanged, this,
            [this](int idx) { emit kindChanged(m_kindCombo->itemData(idx).toString()); });
    layout->addWidget(m_kindCombo);

    m_chipBar = new QWidget(this);
    auto* chipLayout = new QHBoxLayout(m_chipBar);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(4);
    m_chipBar->hide();
    layout->addWidget(m_chipBar);

    m_sortCombo = new QComboBox(this);
    m_sortCombo->setFixedHeight(24);
    m_sortCombo->setToolTip(QStringLiteral("Sort search results"));
    m_sortCombo->addItem(QStringLiteral("Relevance"), QStringLiteral("relevance"));
    m_sortCombo->addItem(QStringLiteral("Name"), QStringLiteral("name"));
    m_sortCombo->addItem(QStringLiteral("Date"), QStringLiteral("date"));
    m_sortCombo->addItem(QStringLiteral("Size"), QStringLiteral("size"));
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this,
            [this](int idx) { emit sortModeChanged(m_sortCombo->itemData(idx).toString()); });
    layout->addWidget(m_sortCombo);

    m_refineEdit = new QLineEdit(this);
    m_refineEdit->setPlaceholderText(QStringLiteral("Search in results…"));
    m_refineEdit->setFixedHeight(24);
    m_refineEdit->setMaximumWidth(150);
    m_refineEdit->hide();
    connect(m_refineEdit, &QLineEdit::textChanged, this, &SearchScopeBar::refineChanged);
    layout->addWidget(m_refineEdit);

    m_liveCb = new QCheckBox(QStringLiteral("Live"), this);
    m_liveCb->setToolTip(QStringLiteral("Refresh results when this folder changes"));
    m_liveCb->setChecked(true);
    m_liveCb->setStyleSheet(QStringLiteral("font-size: 11px;"));
    connect(m_liveCb, &QCheckBox::toggled, this, &SearchScopeBar::liveToggled);
    layout->addWidget(m_liveCb);

    m_contentsCb = new QCheckBox(QStringLiteral("Contents"), this);
    m_contentsCb->setToolTip(
        QStringLiteral("Also search inside text files and PDFs (use content:term in the query)"));
    m_contentsCb->setStyleSheet(QStringLiteral("font-size: 11px;"));
    connect(m_contentsCb, &QCheckBox::toggled, this, &SearchScopeBar::contentToggled);
    layout->addWidget(m_contentsCb);

    m_indexCb = new QCheckBox(QStringLiteral("Index"), this);
    m_indexCb->setToolTip(
        QStringLiteral("Use the system file index (Baloo, Tracker, or Recoll) when available"));
    m_indexCb->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_indexCb->hide();
    connect(m_indexCb, &QCheckBox::toggled, this, &SearchScopeBar::indexToggled);
    layout->addWidget(m_indexCb);

    layout->addSpacing(8);

    m_filterSummaryLabel = new QLabel(this);
    m_filterSummaryLabel->setStyleSheet(QStringLiteral("font-size: 10px; color: #888888;"));
    m_filterSummaryLabel->setToolTip(QStringLiteral("Active search filters"));
    m_filterSummaryLabel->setMaximumWidth(280);
    m_filterSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_filterSummaryLabel);

    // Live status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        QStringLiteral("font-size: 11px; color: #888888; font-style: italic;"));
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    m_saveSearchBtn = new QPushButton(QStringLiteral("Save Search..."), this);
    m_saveSearchBtn->setFixedHeight(24);
    m_saveSearchBtn->setToolTip(QStringLiteral("Save this search as a Smart Folder"));
    connect(m_saveSearchBtn, &QPushButton::clicked, this, &SearchScopeBar::saveSearchRequested);
    layout->addWidget(m_saveSearchBtn);

    // Clear search button
    m_clearBtn = new QPushButton(QStringLiteral("✕ Clear"), this);
    m_clearBtn->setFixedHeight(24);
    m_clearBtn->setToolTip(QStringLiteral("Clear Search (Esc)"));
    connect(m_clearBtn, &QPushButton::clicked, this, &SearchScopeBar::clearRequested);
    layout->addWidget(m_clearBtn);

    setCurrentFolderName(QStringLiteral("This Folder"));
    updateScopeButtons();
}

void SearchScopeBar::setCurrentFolderName(const QString& name) {
    m_folderName = name.isEmpty() ? QStringLiteral("This Folder") : name;
    m_thisFolderBtn->setText(QStringLiteral("\"%1\"").arg(m_folderName));
    int btnWidth =
        qMax(80, m_thisFolderBtn->fontMetrics().horizontalAdvance(m_thisFolderBtn->text()) + 20);
    m_thisFolderBtn->setFixedWidth(btnWidth);
}

void SearchScopeBar::setStatusText(const QString& text) {
    m_statusLabel->setText(text);
}

void SearchScopeBar::setFilterSummary(const QString& text) {
    m_filterSummaryLabel->setText(text);
    m_filterSummaryLabel->setToolTip(text.isEmpty() ? QStringLiteral("Active search filters")
                                                    : text);
    m_filterSummaryLabel->setVisible(!text.isEmpty());
}

void SearchScopeBar::setSaveSearchEnabled(bool enabled) {
    m_saveSearchBtn->setEnabled(enabled);
}

void SearchScopeBar::setIndexProvider(const QString& provider) {
    m_indexCb->setVisible(!provider.isEmpty());
    if (!provider.isEmpty()) {
        m_indexCb->setToolTip(QStringLiteral("Use the %1 index when available").arg(provider));
    }
}

bool SearchScopeBar::isIndexEnabled() const {
    return m_indexCb->isChecked();
}

void SearchScopeBar::setFromFilter(const SearchFilter& filter) {
    m_scope = filter.thisPC ? SearchScope::ThisPC : SearchScope::ThisFolder;
    m_thisFolderBtn->setChecked(m_scope == SearchScope::ThisFolder);
    m_thisPCBtn->setChecked(m_scope == SearchScope::ThisPC);
    m_subfoldersCb->blockSignals(true);
    m_subfoldersCb->setChecked(filter.recursive);
    m_subfoldersCb->blockSignals(false);
    m_subfoldersCb->setVisible(m_scope == SearchScope::ThisFolder);
    m_contentsCb->blockSignals(true);
    m_contentsCb->setChecked(filter.searchContents);
    m_contentsCb->blockSignals(false);
    m_indexCb->blockSignals(true);
    m_indexCb->setChecked(filter.useIndex);
    m_indexCb->blockSignals(false);
    m_kindCombo->blockSignals(true);
    int kindIndex = 0;
    for (int i = 0; i < m_kindCombo->count(); ++i) {
        if (m_kindCombo->itemData(i).toString().compare(filter.kindFilter, Qt::CaseInsensitive) ==
            0) {
            kindIndex = i;
            break;
        }
    }
    m_kindCombo->setCurrentIndex(kindIndex);
    m_kindCombo->blockSignals(false);
    setFilterSummary(SearchQuery::summary(filter));
    updateScopeButtons();
    updateLiveVisibility();
}

void SearchScopeBar::reset() {
    m_scope = SearchScope::ThisFolder;
    m_thisFolderBtn->setChecked(true);
    m_thisPCBtn->setChecked(false);
    m_subfoldersCb->blockSignals(true);
    m_subfoldersCb->setChecked(false);
    m_subfoldersCb->blockSignals(false);
    m_subfoldersCb->setVisible(true);
    m_contentsCb->blockSignals(true);
    m_contentsCb->setChecked(false);
    m_contentsCb->blockSignals(false);
    m_indexCb->blockSignals(true);
    m_indexCb->setChecked(false);
    m_indexCb->blockSignals(false);
    m_kindCombo->blockSignals(true);
    m_kindCombo->setCurrentIndex(0);
    m_kindCombo->blockSignals(false);
    m_filterSummaryLabel->clear();
    m_filterSummaryLabel->hide();
    m_statusLabel->clear();
    setChips({});
    clearRefine();
    updateScopeButtons();
    setLiveVisible(true);
}

QString SearchScopeBar::currentKind() const {
    return m_kindCombo->currentData().toString();
}

bool SearchScopeBar::isSubfolders() const {
    return m_subfoldersCb->isChecked();
}

bool SearchScopeBar::isContentSearch() const {
    return m_contentsCb->isChecked();
}

void SearchScopeBar::setContentSearch(bool enabled) {
    if (m_contentsCb->isChecked() == enabled)
        return;
    const QSignalBlocker blocker(m_contentsCb);
    m_contentsCb->setChecked(enabled);
}

void SearchScopeBar::setChips(const QVector<SearchQuery::Chip>& chips) {
    if (!m_chipBar)
        return;
    auto* chipLayout = qobject_cast<QHBoxLayout*>(m_chipBar->layout());
    if (!chipLayout)
        return;
    while (QLayoutItem* item = chipLayout->takeAt(0)) {
        if (QWidget* widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_chipButtons.clear();

    constexpr int MaxVisibleChips = 4;
    const int visibleCount = qMin(MaxVisibleChips, static_cast<int>(chips.size()));
    for (int i = 0; i < visibleCount; ++i) {
        auto* button = new QToolButton(m_chipBar);
        button->setText(chips.at(i).label + QStringLiteral("  ✕"));
        button->setToolTip(chips.at(i).token);
        button->setAutoRaise(true);
        button->setFixedHeight(22);
        const QString token = chips.at(i).token;
        connect(button, &QToolButton::clicked, this, [this, token]() { emit chipRemoved(token); });
        chipLayout->addWidget(button);
        m_chipButtons.append(button);
    }
    if (chips.size() > MaxVisibleChips) {
        auto* more = new QToolButton(m_chipBar);
        more->setText(QStringLiteral("+%1").arg(chips.size() - MaxVisibleChips));
        more->setAutoRaise(true);
        more->setFixedHeight(22);
        auto* menu = new QMenu(more);
        for (int i = MaxVisibleChips; i < chips.size(); ++i) {
            const QString token = chips.at(i).token;
            QAction* action = menu->addAction(chips.at(i).label);
            connect(action, &QAction::triggered, this,
                    [this, token]() { emit chipRemoved(token); });
        }
        more->setMenu(menu);
        more->setPopupMode(QToolButton::InstantPopup);
        chipLayout->addWidget(more);
        m_chipButtons.append(more);
    }
    m_chipBar->setVisible(!chips.isEmpty());
    updateThemeStyles(m_isDark);
}

void SearchScopeBar::setRefineVisible(bool visible) {
    if (m_refineEdit)
        m_refineEdit->setVisible(visible);
}

void SearchScopeBar::clearRefine() {
    if (!m_refineEdit)
        return;
    const QSignalBlocker blocker(m_refineEdit);
    m_refineEdit->clear();
}

QString SearchScopeBar::refineText() const {
    return m_refineEdit ? m_refineEdit->text().trimmed() : QString();
}

void SearchScopeBar::setLiveEnabled(bool enabled) {
    if (!m_liveCb)
        return;
    const QSignalBlocker blocker(m_liveCb);
    m_liveCb->setChecked(enabled);
}

void SearchScopeBar::setLiveVisible(bool visible) {
    if (m_liveCb)
        m_liveCb->setVisible(visible);
}

bool SearchScopeBar::isLiveEnabled() const {
    return m_liveCb && m_liveCb->isChecked();
}

void SearchScopeBar::setSortMode(const QString& mode) {
    if (!m_sortCombo)
        return;
    const QSignalBlocker blocker(m_sortCombo);
    const int index = m_sortCombo->findData(mode);
    m_sortCombo->setCurrentIndex(index >= 0 ? index : 0);
}

QString SearchScopeBar::sortMode() const {
    return m_sortCombo ? m_sortCombo->currentData().toString() : QStringLiteral("relevance");
}

void SearchScopeBar::updateLiveVisibility() {
    if (m_liveCb)
        m_liveCb->setVisible(m_scope == SearchScope::ThisFolder);
}

void SearchScopeBar::onThisFolderClicked() {
    if (m_scope != SearchScope::ThisFolder) {
        m_scope = SearchScope::ThisFolder;
        updateScopeButtons();
        m_subfoldersCb->setVisible(true);
        updateLiveVisibility();
        emit scopeChanged(m_scope);
    } else {
        m_thisFolderBtn->setChecked(true);
    }
}

void SearchScopeBar::onThisPCClicked() {
    if (m_scope != SearchScope::ThisPC) {
        m_scope = SearchScope::ThisPC;
        updateScopeButtons();
        m_subfoldersCb->setVisible(false);
        updateLiveVisibility();
        emit scopeChanged(m_scope);
    } else {
        m_thisPCBtn->setChecked(true);
    }
}

void SearchScopeBar::updateScopeButtons() {
    m_thisFolderBtn->setChecked(m_scope == SearchScope::ThisFolder);
    m_thisPCBtn->setChecked(m_scope == SearchScope::ThisPC);
    updateThemeStyles(m_isDark);
}

void SearchScopeBar::updateThemeStyles(bool isDark) {
    m_isDark = isDark;
    const auto& c = ThemeManager::instance().colors();
    QString barBg = c.windowAlt.name();
    QString borderCol = c.border.name();
    QString textCol = c.text.name();
    QString activeBg = c.view.name();
    QString activeText = c.text.name();
    QString inactiveBg = c.button.name();
    QString inactiveText = c.subText.name();
    QString accent = c.accent.name();
    QString hoverBg = isDark ? c.button.lighter(120).name() : c.button.darker(108).name();

    setStyleSheet(
        QStringLiteral("SearchScopeBar { background-color: %1; border-bottom: 1px solid %2; }")
            .arg(barBg, borderCol));

    QString leftBtnStyle =
        QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; border-right: none; "
            "border-top-left-radius: 4px; border-bottom-left-radius: 4px; padding: 2px 8px; "
            "font-size: 11px; font-weight: %4; }"
            "QPushButton:hover { background: %5; }")
            .arg(m_scope == SearchScope::ThisFolder ? activeBg : inactiveBg,
                 m_scope == SearchScope::ThisFolder ? activeText : inactiveText, borderCol,
                 m_scope == SearchScope::ThisFolder ? QStringLiteral("bold")
                                                    : QStringLiteral("normal"),
                 m_scope == SearchScope::ThisFolder ? activeBg : hoverBg);
    m_thisFolderBtn->setStyleSheet(leftBtnStyle);

    QString rightBtnStyle =
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-top-right-radius: 4px; border-bottom-right-radius: 4px; padding: "
                       "2px 8px; font-size: 11px; font-weight: %4; }"
                       "QPushButton:hover { background: %5; }")
            .arg(m_scope == SearchScope::ThisPC ? activeBg : inactiveBg,
                 m_scope == SearchScope::ThisPC ? activeText : inactiveText, borderCol,
                 m_scope == SearchScope::ThisPC ? QStringLiteral("bold") : QStringLiteral("normal"),
                 m_scope == SearchScope::ThisPC ? activeBg : hoverBg);
    m_thisPCBtn->setStyleSheet(rightBtnStyle);

    m_kindCombo->setStyleSheet(
        QStringLiteral("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 4px; padding: 2px 6px; font-size: 11px; }"
                       "QComboBox::drop-down { border: none; width: 14px; }"
                       "QComboBox QAbstractItemView { background: %1; color: %2; "
                       "selection-background-color: %4; selection-color: #ffffff; }")
            .arg(inactiveBg, textCol, borderCol, accent));

    if (m_sortCombo) {
        m_sortCombo->setStyleSheet(
            QStringLiteral("QComboBox { background: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 4px; padding: 2px 6px; font-size: 11px; }"
                           "QComboBox::drop-down { border: none; width: 14px; }"
                           "QComboBox QAbstractItemView { background: %1; color: %2; "
                           "selection-background-color: %4; selection-color: #ffffff; }")
                .arg(inactiveBg, textCol, borderCol, accent));
    }
    if (m_refineEdit) {
        m_refineEdit->setStyleSheet(
            QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 4px; padding: 2px 6px; font-size: 11px; }")
                .arg(inactiveBg, textCol, borderCol));
    }
    for (QToolButton* button : m_chipButtons) {
        if (!button)
            continue;
        button->setStyleSheet(
            QStringLiteral("QToolButton { background: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 10px; padding: 1px 7px; font-size: 10px; }"
                           "QToolButton:hover { background: %4; }")
                .arg(activeBg, textCol, borderCol, hoverBg));
    }

    m_clearBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 4px; padding: 2px 10px; font-size: 11px; }"
                       "QPushButton:hover { background: %4; color: #ff5555; }")
            .arg(inactiveBg, textCol, borderCol, hoverBg));

    m_saveSearchBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 4px; padding: 2px 10px; font-size: 11px; }"
                       "QPushButton:hover { background: %4; }")
            .arg(inactiveBg, textCol, borderCol, hoverBg));

    if (m_contentsCb)
        m_contentsCb->setStyleSheet(
            QStringLiteral("QCheckBox { color: %1; font-size: 11px; }").arg(textCol));
    if (m_liveCb)
        m_liveCb->setStyleSheet(
            QStringLiteral("QCheckBox { color: %1; font-size: 11px; }").arg(textCol));
    if (m_filterSummaryLabel)
        m_filterSummaryLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 10px; }").arg(inactiveText));
}
