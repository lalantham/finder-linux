#include "TabManager.h"
#include "TearOffTabBar.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QProcess>
#include <QTabBar>
#include <QVBoxLayout>

SplitTabWidget::SplitTabWidget(const QString& path, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    layout->addWidget(m_splitter);

    m_primaryPage = new TabPage(path, this);
    m_activePage = m_primaryPage;
    m_primaryPage->setProperty("splitPane", false);
    m_primaryPage->setActive(true);
    setupPage(m_primaryPage);
    m_splitter->addWidget(m_primaryPage);
}

void SplitTabWidget::setupPage(TabPage* page) {
    connect(page, &TabPage::activated, this, [this, page]() {
        if (m_activePage != page) {
            m_activePage = page;
            if (m_primaryPage)
                m_primaryPage->setActive(page == m_primaryPage);
            if (m_secondaryPage)
                m_secondaryPage->setActive(page == m_secondaryPage);
            emit activePageChanged(page);
            emit pagePathChanged(page, page->currentPath());
            emit pageHistoryChanged(page);
        }
    });
    connect(page, &TabPage::pathChanged, this, [this, page](const QString& p) {
        if (m_syncEnabled && !m_syncing && m_secondaryPage) {
            TabPage* other = (page == m_primaryPage) ? m_secondaryPage : m_primaryPage;
            if (other && other->currentPath() != p) {
                m_syncing = true;
                other->navigateTo(p);
                m_syncing = false;
            }
        }
        emit pagePathChanged(page, p);
    });
    connect(page, &TabPage::historyChanged, this,
            [this, page]() { emit pageHistoryChanged(page); });
    connect(page, &TabPage::statusMessageChanged, this,
            [this, page](const QString& msg) { emit pageStatusChanged(page, msg); });
    connect(page, &TabPage::quickLookRequested, this, &SplitTabWidget::quickLookRequested);
    connect(page, &TabPage::getInfoRequested, this, &SplitTabWidget::getInfoRequested);
    connect(page, &TabPage::contextMenuRequested, this, &SplitTabWidget::contextMenuRequested);
    connect(page, &TabPage::fileDropRequested, this, &SplitTabWidget::fileDropRequested);
    connect(page, &TabPage::searchClearRequested, this, &SplitTabWidget::searchClearRequested);
    connect(page, &TabPage::saveSearchRequested, this, &SplitTabWidget::saveSearchRequested);
    connect(page, &TabPage::itemRenamed, this, &SplitTabWidget::itemRenamed);
}

void SplitTabWidget::toggleSplit() {
    if (!m_secondaryPage) {
        QString initial = m_primaryPage ? m_primaryPage->currentPath() : QDir::homePath();
        m_secondaryPage = new TabPage(initial, this);
        setupPage(m_secondaryPage);
        m_splitter->addWidget(m_secondaryPage);
        m_splitter->setSizes({width() / 2, width() / 2});
        if (m_primaryPage) {
            m_primaryPage->setProperty("splitPane", true);
            m_primaryPage->setActive(false);
        }
        m_secondaryPage->setProperty("splitPane", true);
        m_secondaryPage->setActive(true);
        m_activePage = m_secondaryPage;
        emit activePageChanged(m_secondaryPage);
        emit pagePathChanged(m_secondaryPage, m_secondaryPage->currentPath());
        emit pageHistoryChanged(m_secondaryPage);
    } else {
        m_secondaryPage->deleteLater();
        m_secondaryPage = nullptr;
        m_activePage = m_primaryPage;
        if (m_syncEnabled) {
            m_syncEnabled = false;
            emit syncEnabledChanged(false);
        }
        if (m_primaryPage) {
            m_primaryPage->setProperty("splitPane", false);
            m_primaryPage->setActive(true);
            emit activePageChanged(m_primaryPage);
            emit pagePathChanged(m_primaryPage, m_primaryPage->currentPath());
            emit pageHistoryChanged(m_primaryPage);
        }
    }
}

void SplitTabWidget::setSyncEnabled(bool enabled) {
    if (m_syncEnabled == enabled) {
        return;
    }
    m_syncEnabled = enabled;

    if (enabled && m_secondaryPage && m_activePage) {
        TabPage* other = otherPage(m_activePage);
        if (other && other->currentPath() != m_activePage->currentPath()) {
            m_syncing = true;
            other->navigateTo(m_activePage->currentPath());
            m_syncing = false;
        }
    }

    emit syncEnabledChanged(enabled);
}

TabPage* SplitTabWidget::otherPage(TabPage* page) const {
    if (page == m_primaryPage) {
        return m_secondaryPage;
    }
    if (page == m_secondaryPage) {
        return m_primaryPage;
    }
    return nullptr;
}

void SplitTabWidget::refreshAll(const QString& pathToSelect) {
    if (m_primaryPage)
        m_primaryPage->refresh(pathToSelect);
    if (m_secondaryPage)
        m_secondaryPage->refresh(pathToSelect);
}

void SplitTabWidget::setShowHidden(bool show) {
    if (m_primaryPage)
        m_primaryPage->setShowHidden(show);
    if (m_secondaryPage)
        m_secondaryPage->setShowHidden(show);
}

void SplitTabWidget::setCalculateFolderSizes(bool enabled) {
    if (m_primaryPage)
        m_primaryPage->setCalculateFolderSizes(enabled);
    if (m_secondaryPage)
        m_secondaryPage->setCalculateFolderSizes(enabled);
}

// TabManager
TabManager::TabManager(QWidget* parent) : QTabWidget(parent) {
    auto* tearOffBar = new TearOffTabBar(this);
    setTabBar(tearOffBar);
    connect(tearOffBar, &TearOffTabBar::tearOffRequested, this, &TabManager::tearOffTab);

    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, &QTabWidget::tabCloseRequested, this, &TabManager::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &TabManager::onCurrentChanged);

    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QTabBar::customContextMenuRequested, this, &TabManager::onTabContextMenu);
}

SplitTabWidget* TabManager::currentSplitTab() const {
    return qobject_cast<SplitTabWidget*>(currentWidget());
}

SplitTabWidget* TabManager::splitTabAt(int index) const {
    return qobject_cast<SplitTabWidget*>(widget(index));
}

TabPage* TabManager::currentPage() const {
    SplitTabWidget* st = currentSplitTab();
    return st ? st->activePage() : nullptr;
}

TabPage* TabManager::pageAt(int index) const {
    SplitTabWidget* st = splitTabAt(index);
    return st ? st->activePage() : nullptr;
}

bool TabManager::isSplit() const {
    SplitTabWidget* st = currentSplitTab();
    return st && st->isSplit();
}

void TabManager::toggleSplit() {
    if (SplitTabWidget* st = currentSplitTab()) {
        st->toggleSplit();
    }
}

void TabManager::setSplitSyncEnabled(bool enabled) {
    if (SplitTabWidget* st = currentSplitTab()) {
        st->setSyncEnabled(enabled);
    }
}

bool TabManager::isSplitSyncEnabled() const {
    SplitTabWidget* st = currentSplitTab();
    return st && st->isSyncEnabled();
}

TabPage* TabManager::otherPage() const {
    SplitTabWidget* st = currentSplitTab();
    return st ? st->otherPage(st->activePage()) : nullptr;
}

void TabManager::refreshCurrentTab(const QString& pathToSelect) {
    if (SplitTabWidget* st = currentSplitTab()) {
        st->refreshAll(pathToSelect);
    }
}

TabPage* TabManager::createTab(const QString& path) {
    auto* splitTab = new SplitTabWidget(path, this);
    QFileInfo fi(path);
    QString tabTitle = fi.fileName().isEmpty() ? QStringLiteral("/") : fi.fileName();

    int idx = addTab(splitTab, tabTitle);
    tabBar()->setTabData(idx, QVariant::fromValue(reinterpret_cast<quintptr>(splitTab)));
    setCurrentIndex(idx);

    connect(splitTab, &SplitTabWidget::activePageChanged, this, [this, splitTab](TabPage* page) {
        if (splitTab == currentSplitTab()) {
            emit activePageChanged(page);
        }
    });

    connect(splitTab, &SplitTabWidget::pagePathChanged, this,
            [this, splitTab](TabPage* page, const QString& p) {
                if (page == splitTab->primaryPage()) {
                    onPagePathChanged(p);
                }
                if (splitTab == currentSplitTab() && page == splitTab->activePage()) {
                    emit activePagePathChanged(page, p);
                }
            });

    connect(splitTab, &SplitTabWidget::pageHistoryChanged, this, [this, splitTab](TabPage* page) {
        if (splitTab == currentSplitTab() && page == splitTab->activePage()) {
            emit activePageHistoryChanged(page);
        }
    });

    connect(splitTab, &SplitTabWidget::pageStatusChanged, this,
            [this, splitTab](TabPage* page, const QString& msg) {
                if (splitTab == currentSplitTab() && page == splitTab->activePage()) {
                    emit activePageStatusChanged(msg);
                }
            });

    connect(splitTab, &SplitTabWidget::quickLookRequested, this, &TabManager::quickLookRequested);
    connect(splitTab, &SplitTabWidget::getInfoRequested, this, &TabManager::getInfoRequested);
    connect(splitTab, &SplitTabWidget::contextMenuRequested, this,
            &TabManager::contextMenuRequested);
    connect(splitTab, &SplitTabWidget::fileDropRequested, this, &TabManager::fileDropRequested);
    connect(splitTab, &SplitTabWidget::searchClearRequested, this,
            &TabManager::searchClearRequested);
    connect(splitTab, &SplitTabWidget::saveSearchRequested, this, &TabManager::saveSearchRequested);
    connect(splitTab, &SplitTabWidget::itemRenamed, this, &TabManager::itemRenamed);

    tabBar()->setVisible(count() > 1);

    emit activePageChanged(splitTab->activePage());
    return splitTab->primaryPage();
}

void TabManager::closeCurrentTab() {
    if (count() > 1) {
        onTabCloseRequested(currentIndex());
    }
}

void TabManager::closeTab(int index) {
    if (count() <= 1 || index < 0 || index >= count())
        return;
    if (TabPage* page = pageAt(index)) {
        m_closedTabHistory.append(page->currentPath());
    }
    QWidget* w = widget(index);
    removeTab(index);
    delete w;
    tabBar()->setVisible(count() > 1);
    emit activePageChanged(currentPage());
}

void TabManager::closeOtherTabs(int index) {
    if (count() <= 1 || index < 0 || index >= count())
        return;
    for (int i = count() - 1; i >= 0; --i) {
        if (i == index)
            continue;
        if (TabPage* page = pageAt(i)) {
            m_closedTabHistory.append(page->currentPath());
        }
        QWidget* w = widget(i);
        removeTab(i);
        delete w;
    }
    setCurrentIndex(0);
    tabBar()->setVisible(count() > 1);
    emit activePageChanged(currentPage());
}

void TabManager::closeTabsToRight(int index) {
    for (int i = count() - 1; i > index; --i) {
        if (TabPage* page = pageAt(i)) {
            m_closedTabHistory.append(page->currentPath());
        }
        QWidget* w = widget(i);
        removeTab(i);
        delete w;
    }
    tabBar()->setVisible(count() > 1);
    emit activePageChanged(currentPage());
}

void TabManager::duplicateTab(int index) {
    if (TabPage* page = pageAt(index)) {
        createTab(page->currentPath());
    }
}

TabPage* TabManager::reopenLastClosedTab() {
    if (m_closedTabHistory.isEmpty()) {
        return nullptr;
    }
    QString path = m_closedTabHistory.takeLast();
    return createTab(path);
}

void TabManager::activateNextTab() {
    if (count() < 2)
        return;
    setCurrentIndex((currentIndex() + 1) % count());
}

void TabManager::activatePreviousTab() {
    if (count() < 2)
        return;
    setCurrentIndex((currentIndex() - 1 + count()) % count());
}

void TabManager::tearOffTab(int index) {
    if (index < 0 || index >= count()) {
        return;
    }

    SplitTabWidget* splitTab = splitTabAt(index);
    if (!splitTab) {
        return;
    }

    TabPage* page = splitTab->activePage();
    const QString path = page ? page->currentPath() : QDir::homePath();

    const bool launched = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                  {QStringLiteral("--new-window"), path});
    if (!launched) {
        return;
    }

    if (count() <= 1) {
        // The tab was the window's only content; close the now-empty window.
        window()->close();
    } else {
        closeTab(index);
    }
}

void TabManager::onTabContextMenu(const QPoint& pos) {
    const int index = tabBar()->tabAt(pos);
    QMenu menu(this);

    if (index >= 0) {
        TabPage* page = pageAt(index);
        menu.addAction(QStringLiteral("New Tab"), this, [this, page]() {
            createTab(page ? page->currentPath() : QDir::homePath());
        });
        menu.addAction(QStringLiteral("Duplicate Tab"), this,
                       [this, index]() { duplicateTab(index); });
        menu.addAction(QStringLiteral("Move Tab to New Window"), this,
                       [this, index]() { tearOffTab(index); });
        QAction* reopenAction = menu.addAction(QStringLiteral("Reopen Closed Tab"), this,
                                               &TabManager::reopenLastClosedTab);
        reopenAction->setEnabled(!m_closedTabHistory.isEmpty());
        reopenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
        menu.addSeparator();

        QAction* closeAction =
            menu.addAction(QStringLiteral("Close Tab"), this, [this, index]() { closeTab(index); });
        closeAction->setEnabled(count() > 1);

        QAction* closeOthers = menu.addAction(QStringLiteral("Close Other Tabs"), this,
                                              [this, index]() { closeOtherTabs(index); });
        closeOthers->setEnabled(count() > 1);

        QAction* closeRight = menu.addAction(QStringLiteral("Close Tabs to the Right"), this,
                                             [this, index]() { closeTabsToRight(index); });
        closeRight->setEnabled(index + 1 < count());
    } else {
        menu.addAction(QStringLiteral("New Tab"), this, [this]() { createTab(QDir::homePath()); });
        QAction* reopenAction = menu.addAction(QStringLiteral("Reopen Closed Tab"), this,
                                               &TabManager::reopenLastClosedTab);
        reopenAction->setEnabled(!m_closedTabHistory.isEmpty());
        reopenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    }

    menu.exec(tabBar()->mapToGlobal(pos));
}

void TabManager::setShowHidden(bool show) {
    for (int i = 0; i < count(); ++i) {
        if (auto* st = splitTabAt(i)) {
            st->setShowHidden(show);
        }
    }
}

void TabManager::setCalculateFolderSizes(bool enabled) {
    for (int i = 0; i < count(); ++i) {
        if (auto* st = splitTabAt(i)) {
            st->setCalculateFolderSizes(enabled);
        }
    }
}

void TabManager::onTabCloseRequested(int index) {
    closeTab(index);
}

void TabManager::onCurrentChanged(int /*index*/) {
    TabPage* page = currentPage();
    emit activePageChanged(page);
    if (page) {
        emit activePagePathChanged(page, page->currentPath());
        emit activePageHistoryChanged(page);
    }
}

void TabManager::onPagePathChanged(const QString& path) {
    for (int i = 0; i < count(); ++i) {
        if (auto* st = splitTabAt(i)) {
            if (sender() == st || (st->primaryPage() && sender() == st->primaryPage())) {
                QFileInfo fi(path);
                QString title = fi.fileName().isEmpty() ? QStringLiteral("/") : fi.fileName();
                setTabText(i, title);
                break;
            }
        }
    }
}
