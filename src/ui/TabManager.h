#pragma once

#include "TabPage.h"
#include <QSplitter>
#include <QTabWidget>

class SplitTabWidget : public QWidget {
    Q_OBJECT

public:
    explicit SplitTabWidget(const QString& path, QWidget* parent = nullptr);

    [[nodiscard]] TabPage* activePage() const {
        return m_activePage;
    }
    [[nodiscard]] TabPage* primaryPage() const {
        return m_primaryPage;
    }
    [[nodiscard]] TabPage* secondaryPage() const {
        return m_secondaryPage;
    }

    [[nodiscard]] bool isSplit() const {
        return m_secondaryPage != nullptr;
    }
    void toggleSplit();
    void setSyncEnabled(bool enabled);
    [[nodiscard]] bool isSyncEnabled() const noexcept {
        return m_syncEnabled;
    }
    [[nodiscard]] TabPage* otherPage(TabPage* page) const;
    void refreshAll(const QString& pathToSelect = QString());
    void setShowHidden(bool show);
    void setCalculateFolderSizes(bool enabled);

signals:
    void activePageChanged(TabPage* page);
    void pagePathChanged(TabPage* page, const QString& path);
    void pageHistoryChanged(TabPage* page);
    void pageStatusChanged(TabPage* page, const QString& message);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                              const QString& currentDir);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory,
                           Qt::DropAction action);
    void searchClearRequested();
    void saveSearchRequested();
    void itemRenamed(const QString& oldPath, const QString& newPath);
    void syncEnabledChanged(bool enabled);

private:
    void setupPage(TabPage* page);

    QSplitter* m_splitter{nullptr};
    TabPage* m_primaryPage{nullptr};
    TabPage* m_secondaryPage{nullptr};
    TabPage* m_activePage{nullptr};
    bool m_syncEnabled{false};
    bool m_syncing{false};
};

class TabManager : public QTabWidget {
    Q_OBJECT

public:
    explicit TabManager(QWidget* parent = nullptr);

    TabPage* createTab(const QString& path);
    void closeCurrentTab();
    void closeTab(int index);
    void closeOtherTabs(int index);
    void closeTabsToRight(int index);
    void duplicateTab(int index);
    TabPage* reopenLastClosedTab();
    [[nodiscard]] bool canReopenClosedTab() const noexcept {
        return !m_closedTabHistory.isEmpty();
    }
    void activateNextTab();
    void activatePreviousTab();
    [[nodiscard]] TabPage* currentPage() const;
    [[nodiscard]] TabPage* pageAt(int index) const;
    void setShowHidden(bool show);
    void setCalculateFolderSizes(bool enabled);

    [[nodiscard]] bool isSplit() const;
    void toggleSplit();
    void setSplitSyncEnabled(bool enabled);
    [[nodiscard]] bool isSplitSyncEnabled() const;
    [[nodiscard]] TabPage* otherPage() const;
    void refreshCurrentTab(const QString& pathToSelect = QString());

signals:
    void activePageChanged(TabPage* page);
    void activePagePathChanged(TabPage* page, const QString& path);
    void activePageHistoryChanged(TabPage* page);
    void activePageStatusChanged(const QString& message);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                              const QString& currentDir);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory,
                           Qt::DropAction action);
    void searchClearRequested();
    void saveSearchRequested();
    void itemRenamed(const QString& oldPath, const QString& newPath);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);
    void onPagePathChanged(const QString& path);
    void onTabContextMenu(const QPoint& pos);
    void tearOffTab(int index);

private:
    SplitTabWidget* currentSplitTab() const;
    SplitTabWidget* splitTabAt(int index) const;

    QVector<QString> m_closedTabHistory;
};
