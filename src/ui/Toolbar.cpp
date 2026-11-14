#include "Toolbar.h"
#include "AppSettings.h"
#include "StyleHelper.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>

namespace {
QIcon themeIcon(const QStringList& names) {
    for (const QString& name : names) {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull())
            return icon;
    }
    return {};
}
} // namespace

Toolbar::Toolbar(QWidget* parent) : QToolBar(parent) {
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(20, 20));

    setupNavigationControls();

    m_pathCapsule = new QWidget(this);
    m_pathCapsule->setObjectName(QStringLiteral("pathCapsule"));
    m_pathCapsule->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* capsuleLayout = new QHBoxLayout(m_pathCapsule);
    capsuleLayout->setContentsMargins(3, 3, 8, 3);
    capsuleLayout->setSpacing(6);
    capsuleLayout->addWidget(m_navWidget);

    m_breadcrumb = new BreadcrumbBar(m_pathCapsule);
    capsuleLayout->addWidget(m_breadcrumb, 1);
    addWidget(m_pathCapsule);
    connect(m_breadcrumb, &BreadcrumbBar::pathSelected, this, &Toolbar::pathChanged);

    setupViewSwitcher();
    setupActionButtons();
    setupSearch();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            &Toolbar::updateThemeStyles);
    updateThemeStyles(ThemeManager::instance().isDark());
}

void Toolbar::setupNavigationControls() {
    m_navWidget = new QWidget(this);
    auto* navLayout = new QHBoxLayout(m_navWidget);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);

    m_backBtn = new QToolButton(m_navWidget);
    const QIcon backIcon = themeIcon({QStringLiteral("go-previous"), QStringLiteral("arrow-left")});
    if (!backIcon.isNull())
        m_backBtn->setIcon(backIcon);
    else
        m_backBtn->setText(QStringLiteral("‹"));
    QFont f = m_backBtn->font();
    f.setPointSize(14);
    f.setBold(true);
    m_backBtn->setFont(f);
    m_backBtn->setToolTip(QStringLiteral("Back (Alt+Left)"));
    m_backBtn->setEnabled(false);

    m_fwdBtn = new QToolButton(m_navWidget);
    const QIcon forwardIcon = themeIcon({QStringLiteral("go-next"), QStringLiteral("arrow-right")});
    if (!forwardIcon.isNull())
        m_fwdBtn->setIcon(forwardIcon);
    else
        m_fwdBtn->setText(QStringLiteral("›"));
    m_fwdBtn->setFont(f);
    m_fwdBtn->setToolTip(QStringLiteral("Forward (Alt+Right)"));
    m_fwdBtn->setEnabled(false);

    navLayout->addWidget(m_backBtn);
    navLayout->addWidget(m_fwdBtn);

    connect(m_backBtn, &QToolButton::clicked, this, &Toolbar::backRequested);
    connect(m_fwdBtn, &QToolButton::clicked, this, &Toolbar::forwardRequested);
}

void Toolbar::setupViewSwitcher() {
    auto* viewWidget = new QWidget(this);
    auto* viewLayout = new QHBoxLayout(viewWidget);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->setSpacing(0);

    m_viewGroup = new QButtonGroup(this);
    m_viewGroup->setExclusive(true);

    m_iconViewBtn = new QToolButton(viewWidget);
    const QIcon iconViewIcon =
        themeIcon({QStringLiteral("view-grid"), QStringLiteral("view-list-icons")});
    if (!iconViewIcon.isNull())
        m_iconViewBtn->setIcon(iconViewIcon);
    else
        m_iconViewBtn->setText(QStringLiteral("⊞"));
    m_iconViewBtn->setCheckable(true);
    m_iconViewBtn->setStyleSheet(StyleHelper::segmentedButtonLeftStyle());
    m_iconViewBtn->setToolTip(QStringLiteral("as Icons"));

    m_listViewBtn = new QToolButton(viewWidget);
    const QIcon listViewIcon =
        themeIcon({QStringLiteral("view-list-details"), QStringLiteral("view-list-text")});
    if (!listViewIcon.isNull())
        m_listViewBtn->setIcon(listViewIcon);
    else
        m_listViewBtn->setText(QStringLiteral("☰"));
    m_listViewBtn->setCheckable(true);
    m_listViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle());
    m_listViewBtn->setToolTip(QStringLiteral("as List"));

    m_colViewBtn = new QToolButton(viewWidget);
    const QIcon columnViewIcon =
        themeIcon({QStringLiteral("view-list-tree"), QStringLiteral("view-column")});
    if (!columnViewIcon.isNull())
        m_colViewBtn->setIcon(columnViewIcon);
    else
        m_colViewBtn->setText(QStringLiteral("☷"));
    m_colViewBtn->setCheckable(true);
    m_colViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle());
    m_colViewBtn->setToolTip(QStringLiteral("as Columns"));

    m_galViewBtn = new QToolButton(viewWidget);
    const QIcon galleryViewIcon =
        themeIcon({QStringLiteral("view-preview"), QStringLiteral("view-gallery")});
    if (!galleryViewIcon.isNull())
        m_galViewBtn->setIcon(galleryViewIcon);
    else
        m_galViewBtn->setText(QStringLiteral("▣"));
    m_galViewBtn->setCheckable(true);
    m_galViewBtn->setStyleSheet(StyleHelper::segmentedButtonRightStyle());
    m_galViewBtn->setToolTip(QStringLiteral("as Gallery"));

    m_viewGroup->addButton(m_iconViewBtn, static_cast<int>(ViewMode::Icon));
    m_viewGroup->addButton(m_listViewBtn, static_cast<int>(ViewMode::List));
    m_viewGroup->addButton(m_colViewBtn, static_cast<int>(ViewMode::Column));
    m_viewGroup->addButton(m_galViewBtn, static_cast<int>(ViewMode::Gallery));

    viewLayout->addWidget(m_iconViewBtn);
    viewLayout->addWidget(m_listViewBtn);
    viewLayout->addWidget(m_colViewBtn);
    viewLayout->addWidget(m_galViewBtn);

    addWidget(viewWidget);

    connect(m_viewGroup, &QButtonGroup::idClicked, this, &Toolbar::onViewButtonClicked);
    m_iconViewBtn->setChecked(true);
}

void Toolbar::setupActionButtons() {
    m_viewOptionsBtn = new QToolButton(this);
    const QIcon optionsIcon =
        themeIcon({QStringLiteral("view-sort-ascending"), QStringLiteral("view-choose")});
    if (!optionsIcon.isNull())
        m_viewOptionsBtn->setIcon(optionsIcon);
    else
        m_viewOptionsBtn->setText(QStringLiteral("⌄"));
    m_viewOptionsBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_viewOptionsBtn->setPopupMode(QToolButton::InstantPopup);
    m_viewOptionsBtn->setToolTip(QStringLiteral("View Options"));
    auto* viewOptionsMenu = new QMenu(m_viewOptionsBtn);
    auto* optionsAction = viewOptionsMenu->addAction(QStringLiteral("Show View Options..."));
    connect(optionsAction, &QAction::triggered, this, &Toolbar::viewOptionsRequested);
    m_viewOptionsBtn->setMenu(viewOptionsMenu);
    addWidget(m_viewOptionsBtn);

    m_tagBtn = new QToolButton(this);
    const QIcon tagIcon = themeIcon({QStringLiteral("tag"), QStringLiteral("tag-symbolic")});
    if (!tagIcon.isNull())
        m_tagBtn->setIcon(tagIcon);
    else
        m_tagBtn->setText(QStringLiteral("◉"));
    m_tagBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_tagBtn->setPopupMode(QToolButton::InstantPopup);
    m_tagBtn->setToolTip(QStringLiteral("Tags"));
    m_tagMenu = new QMenu(m_tagBtn);
    connect(m_tagMenu, &QMenu::aboutToShow, this, &Toolbar::rebuildTagMenu);
    m_tagBtn->setMenu(m_tagMenu);
    rebuildTagMenu();
    addWidget(m_tagBtn);

    m_moreBtn = new QToolButton(this);
    const QIcon moreIcon =
        themeIcon({QStringLiteral("overflow-menu"), QStringLiteral("application-menu")});
    if (!moreIcon.isNull())
        m_moreBtn->setIcon(moreIcon);
    else
        m_moreBtn->setText(QStringLiteral("···"));
    m_moreBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
    m_moreBtn->setToolTip(QStringLiteral("More Actions"));

    auto* moreMenu = new QMenu(m_moreBtn);
    auto* actNewFolder = moreMenu->addAction(themeIcon({QStringLiteral("folder-new")}),
                                             QStringLiteral("New Folder"));
    connect(actNewFolder, &QAction::triggered, this, &Toolbar::newFolderRequested);
    auto* actQuickLook = moreMenu->addAction(
        themeIcon({QStringLiteral("document-preview"), QStringLiteral("view-preview")}),
        QStringLiteral("Quick Look"));
    connect(actQuickLook, &QAction::triggered, this, &Toolbar::quickLookRequested);
    auto* actGetInfo = moreMenu->addAction(themeIcon({QStringLiteral("dialog-information")}),
                                           QStringLiteral("Get Info"));
    connect(actGetInfo, &QAction::triggered, this, &Toolbar::getInfoRequested);
    moreMenu->addSeparator();
    m_hiddenAction = moreMenu->addAction(QStringLiteral("Show Hidden Files"));
    m_hiddenAction->setCheckable(true);
    m_hiddenAction->setChecked(AppSettings::instance().showHiddenFiles());
    connect(m_hiddenAction, &QAction::triggered, this, &Toolbar::toggleHiddenRequested);
    moreMenu->addSeparator();
    auto* actTerminal = moreMenu->addAction(themeIcon({QStringLiteral("utilities-terminal")}),
                                            QStringLiteral("Open in Terminal"));
    connect(actTerminal, &QAction::triggered, this, &Toolbar::openTerminalRequested);
    m_moreBtn->setMenu(moreMenu);
    addWidget(m_moreBtn);
}

void Toolbar::rebuildTagMenu() {
    if (!m_tagMenu)
        return;
    m_tagMenu->clear();
    for (const TagInfo& tag : TagManager::allTags()) {
        auto* action = m_tagMenu->addAction(TagManager::tagDotIcon(tag.color, 14), tag.name);
        connect(action, &QAction::triggered, this,
                [this, name = tag.name]() { emit tagRequested(name); });
    }
    m_tagMenu->addSeparator();
    auto* clearTags = m_tagMenu->addAction(QStringLiteral("Clear Tags"));
    connect(clearTags, &QAction::triggered, this, &Toolbar::clearTagsRequested);
    m_tagMenu->addSeparator();
    auto* manageTags = m_tagMenu->addAction(QStringLiteral("Manage Tags..."));
    connect(manageTags, &QAction::triggered, this, &Toolbar::manageTagsRequested);
}

void Toolbar::setupSearch() {
    m_searchBtn = new QToolButton(this);
    const QIcon searchIcon =
        themeIcon({QStringLiteral("system-search-symbolic"), QStringLiteral("edit-find"),
                   QStringLiteral("system-search")});
    if (!searchIcon.isNull())
        m_searchBtn->setIcon(searchIcon);
    else
        m_searchBtn->setText(QStringLiteral("⌕"));
    m_searchBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_searchBtn->setToolTip(QStringLiteral("Search (Ctrl+F)"));
    connect(m_searchBtn, &QToolButton::clicked, this, &Toolbar::toggleSearch);
    m_searchBtnAction = addWidget(m_searchBtn);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search"));
    m_searchEdit->setToolTip(
        QStringLiteral("Search. Tokens: kind:image, tag:Red, ext:pdf, "
                       "size:>10MB, modified:>2025-01-01, content:\"invoice\", -exclude"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(270);
    m_searchEdit->setFixedHeight(30);
    m_searchEdit->setStyleSheet(StyleHelper::searchBarStyle());
    m_searchEdit->installEventFilter(this);
    if (!searchIcon.isNull()) {
        m_searchEdit->addAction(searchIcon, QLineEdit::LeadingPosition);
    }
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        const bool wasEmpty = !m_searchHasText;
        m_searchHasText = !text.isEmpty();
        emit searchTextChanged(text);
        if (text.isEmpty() && !wasEmpty) {
            closeSearch();
        }
    });

    // Escape in search line edit clears it and closes the field.
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchEdit);
    connect(escShortcut, &QShortcut::activated, this, [this]() { clearSearch(); });

    m_searchEditAction = addWidget(m_searchEdit);
    m_searchEditAction->setVisible(false);
}

void Toolbar::toggleSearch() {
    if (isSearchVisible()) {
        if (m_searchEdit->text().isEmpty())
            closeSearch();
        else {
            m_searchEdit->setFocus();
            m_searchEdit->selectAll();
        }
    } else {
        openSearch();
    }
}

void Toolbar::openSearch() {
    if (!m_searchEdit || !m_searchBtnAction || !m_searchEditAction)
        return;
    m_searchBtnAction->setVisible(false);
    m_searchEditAction->setVisible(true);
    m_searchEdit->setFocus();
}

void Toolbar::closeSearch() {
    if (!m_searchEdit || !m_searchBtnAction || !m_searchEditAction)
        return;
    m_searchEdit->clearFocus();
    m_searchEditAction->setVisible(false);
    m_searchBtnAction->setVisible(true);
}

void Toolbar::setSearchVisible(bool visible) {
    if (visible)
        openSearch();
    else
        clearSearch();
}

bool Toolbar::isSearchVisible() const {
    return m_searchEditAction && m_searchEditAction->isVisible();
}

void Toolbar::focusSearch() {
    openSearch();
    if (m_searchEdit)
        m_searchEdit->selectAll();
}

void Toolbar::clearSearch() {
    if (m_searchEdit) {
        const QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
        m_searchHasText = false;
    }
    closeSearch();
    emit searchTextChanged(QString());
}

void Toolbar::setSearchText(const QString& text) {
    if (!m_searchEdit || m_searchEdit->text() == text)
        return;
    const QSignalBlocker blocker(m_searchEdit);
    m_searchEdit->setText(text);
    m_searchHasText = !text.isEmpty();
    if (text.isEmpty())
        closeSearch();
    else
        openSearch();
}

bool Toolbar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_searchEdit && event->type() == QEvent::FocusOut) {
        QTimer::singleShot(0, this, [this]() {
            if (m_searchEdit && !m_searchEdit->hasFocus() && m_searchEdit->text().isEmpty()) {
                closeSearch();
            }
        });
    }
    return QToolBar::eventFilter(watched, event);
}

void Toolbar::setPathBarVisible(bool visible) {
    if (m_breadcrumb) {
        m_breadcrumb->setVisible(visible);
    }
}

QString Toolbar::searchQuery() const {
    return m_searchEdit ? m_searchEdit->text() : QString();
}

void Toolbar::setPath(const QString& path) {
    m_breadcrumb->setPath(path);
}

void Toolbar::setCanGoBack(bool can) {
    m_backBtn->setEnabled(can);
}

void Toolbar::setCanGoForward(bool can) {
    m_fwdBtn->setEnabled(can);
}

void Toolbar::setViewMode(ViewMode mode) {
    if (auto* btn = m_viewGroup->button(static_cast<int>(mode))) {
        btn->setChecked(true);
    }
}

void Toolbar::setShowHidden(bool show) {
    if (m_hiddenAction) {
        const QSignalBlocker blocker(m_hiddenAction);
        m_hiddenAction->setChecked(show);
    }
}

void Toolbar::onViewButtonClicked(int id) {
    emit viewModeChanged(static_cast<ViewMode>(id));
}

void Toolbar::updateThemeStyles(bool isDark) {

    if (m_iconViewBtn)
        m_iconViewBtn->setStyleSheet(StyleHelper::segmentedButtonLeftStyle(isDark));
    if (m_listViewBtn)
        m_listViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle(isDark));
    if (m_colViewBtn)
        m_colViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle(isDark));
    if (m_galViewBtn)
        m_galViewBtn->setStyleSheet(StyleHelper::segmentedButtonRightStyle(isDark));

    if (m_viewOptionsBtn)
        m_viewOptionsBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_tagBtn)
        m_tagBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_moreBtn)
        m_moreBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));

    if (m_searchBtn)
        m_searchBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_searchEdit)
        m_searchEdit->setStyleSheet(StyleHelper::searchBarStyle(isDark));
    if (m_breadcrumb)
        m_breadcrumb->updateThemeStyles(isDark);
}
