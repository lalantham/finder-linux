#include "Toolbar.h"
#include "AppSettings.h"
#include "StyleHelper.h"
#include "ThemeManager.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QLabel>
#include <QKeyEvent>
#include <QShortcut>

Toolbar::Toolbar(QWidget* parent)
    : QToolBar(parent)
{
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(20, 20));

    setupNavigationControls();
    addSeparator();

    m_breadcrumb = new BreadcrumbBar(this);
    addWidget(m_breadcrumb);
    connect(m_breadcrumb, &BreadcrumbBar::pathSelected, this, &Toolbar::pathChanged);

    addSeparator();
    setupViewSwitcher();
    addSeparator();
    setupActionButtons();
    addSeparator();
    setupSearch();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &Toolbar::updateThemeStyles);
    updateThemeStyles(ThemeManager::instance().isDark());
}

void Toolbar::setupNavigationControls()
{
    auto* navWidget = new QWidget(this);
    auto* navLayout = new QHBoxLayout(navWidget);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);

    m_backBtn = new QToolButton(navWidget);
    m_backBtn->setText(QStringLiteral("‹"));
    QFont f = m_backBtn->font();
    f.setPointSize(14);
    f.setBold(true);
    m_backBtn->setFont(f);
    m_backBtn->setStyleSheet(StyleHelper::segmentedButtonLeftStyle());
    m_backBtn->setToolTip(QStringLiteral("Back (Alt+Left)"));
    m_backBtn->setEnabled(false);

    m_fwdBtn = new QToolButton(navWidget);
    m_fwdBtn->setText(QStringLiteral("›"));
    m_fwdBtn->setFont(f);
    m_fwdBtn->setStyleSheet(StyleHelper::segmentedButtonRightStyle());
    m_fwdBtn->setToolTip(QStringLiteral("Forward (Alt+Right)"));
    m_fwdBtn->setEnabled(false);

    navLayout->addWidget(m_backBtn);
    navLayout->addWidget(m_fwdBtn);

    addWidget(navWidget);

    connect(m_backBtn, &QToolButton::clicked, this, &Toolbar::backRequested);
    connect(m_fwdBtn, &QToolButton::clicked, this, &Toolbar::forwardRequested);
}

void Toolbar::setupViewSwitcher()
{
    auto* viewWidget = new QWidget(this);
    auto* viewLayout = new QHBoxLayout(viewWidget);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->setSpacing(0);

    m_viewGroup = new QButtonGroup(this);
    m_viewGroup->setExclusive(true);

    m_iconViewBtn = new QToolButton(viewWidget);
    m_iconViewBtn->setText(QStringLiteral("⊞"));
    m_iconViewBtn->setCheckable(true);
    m_iconViewBtn->setStyleSheet(StyleHelper::segmentedButtonLeftStyle());
    m_iconViewBtn->setToolTip(QStringLiteral("as Icons"));

    m_listViewBtn = new QToolButton(viewWidget);
    m_listViewBtn->setText(QStringLiteral("☰"));
    m_listViewBtn->setCheckable(true);
    m_listViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle());
    m_listViewBtn->setToolTip(QStringLiteral("as List"));

    m_colViewBtn = new QToolButton(viewWidget);
    m_colViewBtn->setText(QStringLiteral("☷"));
    m_colViewBtn->setCheckable(true);
    m_colViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle());
    m_colViewBtn->setToolTip(QStringLiteral("as Columns"));

    m_galViewBtn = new QToolButton(viewWidget);
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

void Toolbar::setupActionButtons()
{
    m_newFolderBtn = new QToolButton(this);
    m_newFolderBtn->setText(QStringLiteral("＋"));
    m_newFolderBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_newFolderBtn->setToolTip(QStringLiteral("New Folder (Ctrl+Shift+N)"));
    connect(m_newFolderBtn, &QToolButton::clicked, this, &Toolbar::newFolderRequested);
    addWidget(m_newFolderBtn);

    m_quickLookBtn = new QToolButton(this);
    m_quickLookBtn->setText(QStringLiteral("👁"));
    m_quickLookBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_quickLookBtn->setToolTip(QStringLiteral("Quick Look (Space)"));
    connect(m_quickLookBtn, &QToolButton::clicked, this, &Toolbar::quickLookRequested);
    addWidget(m_quickLookBtn);

    m_getInfoBtn = new QToolButton(this);
    m_getInfoBtn->setText(QStringLiteral("ℹ"));
    m_getInfoBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_getInfoBtn->setToolTip(QStringLiteral("Get Info (Ctrl+I)"));
    connect(m_getInfoBtn, &QToolButton::clicked, this, &Toolbar::getInfoRequested);
    addWidget(m_getInfoBtn);

    m_hiddenBtn = new QToolButton(this);
    m_hiddenBtn->setText(QStringLiteral(".*"));
    m_hiddenBtn->setCheckable(true);
    m_hiddenBtn->setChecked(AppSettings::instance().showHiddenFiles());
    m_hiddenBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_hiddenBtn->setToolTip(QStringLiteral("Show/Hide Hidden Files (Ctrl+H)"));
    connect(m_hiddenBtn, &QToolButton::clicked, this, &Toolbar::toggleHiddenRequested);
    addWidget(m_hiddenBtn);

    m_moreBtn = new QToolButton(this);
    m_moreBtn->setText(QStringLiteral("···"));
    m_moreBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle());
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
    m_moreBtn->setToolTip(QStringLiteral("More Actions"));

    auto* moreMenu = new QMenu(m_moreBtn);
    auto* actTerminal = moreMenu->addAction(QStringLiteral("Open in Terminal"));
    connect(actTerminal, &QAction::triggered, this, &Toolbar::openTerminalRequested);
    m_moreBtn->setMenu(moreMenu);
    addWidget(m_moreBtn);
}

void Toolbar::setupSearch()
{
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(180);
    m_searchEdit->setStyleSheet(StyleHelper::searchBarStyle());
    connect(m_searchEdit, &QLineEdit::textChanged, this, &Toolbar::searchTextChanged);

    // Escape in search line edit clears it
    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchEdit);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        m_searchEdit->clear();
        m_searchEdit->clearFocus();
    });

    addWidget(m_searchEdit);
}

void Toolbar::focusSearch()
{
    if (m_searchEdit) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void Toolbar::clearSearch()
{
    if (m_searchEdit) {
        m_searchEdit->clear();
    }
}

QString Toolbar::searchQuery() const
{
    return m_searchEdit ? m_searchEdit->text() : QString();
}

void Toolbar::setPath(const QString& path)
{
    m_breadcrumb->setPath(path);
}

void Toolbar::setCanGoBack(bool can)
{
    m_backBtn->setEnabled(can);
}

void Toolbar::setCanGoForward(bool can)
{
    m_fwdBtn->setEnabled(can);
}

void Toolbar::setViewMode(ViewMode mode)
{
    if (auto* btn = m_viewGroup->button(static_cast<int>(mode))) {
        btn->setChecked(true);
    }
}

void Toolbar::setShowHidden(bool show)
{
    m_hiddenBtn->setChecked(show);
}

void Toolbar::onViewButtonClicked(int id)
{
    emit viewModeChanged(static_cast<ViewMode>(id));
}

void Toolbar::updateThemeStyles(bool isDark)
{
    if (m_backBtn) m_backBtn->setStyleSheet(StyleHelper::segmentedButtonLeftStyle(isDark));
    if (m_fwdBtn) m_fwdBtn->setStyleSheet(StyleHelper::segmentedButtonRightStyle(isDark));

    if (m_iconViewBtn) m_iconViewBtn->setStyleSheet(StyleHelper::segmentedButtonLeftStyle(isDark));
    if (m_listViewBtn) m_listViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle(isDark));
    if (m_colViewBtn) m_colViewBtn->setStyleSheet(StyleHelper::segmentedButtonMidStyle(isDark));
    if (m_galViewBtn) m_galViewBtn->setStyleSheet(StyleHelper::segmentedButtonRightStyle(isDark));

    if (m_newFolderBtn) m_newFolderBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_quickLookBtn) m_quickLookBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_getInfoBtn) m_getInfoBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_hiddenBtn) m_hiddenBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));
    if (m_moreBtn) m_moreBtn->setStyleSheet(StyleHelper::segmentedButtonSingleStyle(isDark));

    if (m_searchEdit) m_searchEdit->setStyleSheet(StyleHelper::searchBarStyle(isDark));
    if (m_breadcrumb) m_breadcrumb->updateThemeStyles(isDark);
}
