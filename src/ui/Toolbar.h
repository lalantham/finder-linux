#pragma once

#include "AppSettings.h"
#include "BreadcrumbBar.h"
#include <QToolBar>
#include <QToolButton>
#include <QLineEdit>
#include <QButtonGroup>
#include <QAction>

class Toolbar : public QToolBar {
    Q_OBJECT

public:
    explicit Toolbar(QWidget* parent = nullptr);

    void setPath(const QString& path);
    void setCanGoBack(bool can);
    void setCanGoForward(bool can);
    void setViewMode(ViewMode mode);
    void setShowHidden(bool show);
    void updateThemeStyles(bool isDark);

    [[nodiscard]] BreadcrumbBar* breadcrumbBar() const noexcept { return m_breadcrumb; }

signals:
    void backRequested();
    void forwardRequested();
    void upRequested();
    void pathChanged(const QString& path);
    void viewModeChanged(ViewMode mode);
    void newFolderRequested();
    void quickLookRequested();
    void getInfoRequested();
    void toggleHiddenRequested();
    void openTerminalRequested();
    void searchTextChanged(const QString& query);

private slots:
    void onViewButtonClicked(int id);

private:
    void setupNavigationControls();
    void setupViewSwitcher();
    void setupActionButtons();
    void setupSearch();

    // Navigation
    QToolButton* m_backBtn{nullptr};
    QToolButton* m_fwdBtn{nullptr};
    BreadcrumbBar* m_breadcrumb{nullptr};

    // View Switcher
    QButtonGroup* m_viewGroup{nullptr};
    QToolButton* m_iconViewBtn{nullptr};
    QToolButton* m_listViewBtn{nullptr};
    QToolButton* m_colViewBtn{nullptr};
    QToolButton* m_galViewBtn{nullptr};

    // Action buttons
    QToolButton* m_newFolderBtn{nullptr};
    QToolButton* m_quickLookBtn{nullptr};
    QToolButton* m_getInfoBtn{nullptr};
    QToolButton* m_hiddenBtn{nullptr};
    QToolButton* m_moreBtn{nullptr};

    // Search
    QLineEdit* m_searchEdit{nullptr};
};
