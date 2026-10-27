#include "TabManager.h"
#include <QTabBar>
#include <QFileInfo>

TabManager::TabManager(QWidget* parent)
    : QTabWidget(parent)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, &QTabWidget::tabCloseRequested, this, &TabManager::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &TabManager::onCurrentChanged);
}

TabPage* TabManager::createTab(const QString& path)
{
    auto* page = new TabPage(path, this);
    QFileInfo fi(path);
    QString tabTitle = fi.fileName().isEmpty() ? QStringLiteral("/") : fi.fileName();

    int idx = addTab(page, tabTitle);
    setCurrentIndex(idx);

    connect(page, &TabPage::pathChanged, this, &TabManager::onPagePathChanged);
    connect(page, &TabPage::quickLookRequested, this, &TabManager::quickLookRequested);
    connect(page, &TabPage::getInfoRequested, this, &TabManager::getInfoRequested);
    connect(page, &TabPage::contextMenuRequested, this, &TabManager::contextMenuRequested);

    // If only 1 tab, hide tab bar just like Finder does unless multiple tabs open
    tabBar()->setVisible(count() > 1);

    emit activePageChanged(page);
    return page;
}

void TabManager::closeCurrentTab()
{
    if (count() > 1) {
        onTabCloseRequested(currentIndex());
    }
}

TabPage* TabManager::currentPage() const
{
    return qobject_cast<TabPage*>(currentWidget());
}

void TabManager::onTabCloseRequested(int index)
{
    if (count() <= 1) return;
    QWidget* w = widget(index);
    removeTab(index);
    delete w;

    tabBar()->setVisible(count() > 1);
    emit activePageChanged(currentPage());
}

void TabManager::onCurrentChanged(int /*index*/)
{
    emit activePageChanged(currentPage());
}

void TabManager::onPagePathChanged(const QString& path)
{
    auto* senderPage = qobject_cast<TabPage*>(sender());
    if (!senderPage) return;
    int idx = indexOf(senderPage);
    if (idx >= 0) {
        QFileInfo fi(path);
        QString title = fi.fileName().isEmpty() ? QStringLiteral("/") : fi.fileName();
        setTabText(idx, title);
    }
}
