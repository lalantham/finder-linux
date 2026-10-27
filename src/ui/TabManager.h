#pragma once

#include "TabPage.h"
#include <QTabWidget>

class TabManager : public QTabWidget {
    Q_OBJECT

public:
    explicit TabManager(QWidget* parent = nullptr);

    TabPage* createTab(const QString& path);
    void closeCurrentTab();
    [[nodiscard]] TabPage* currentPage() const;

signals:
    void activePageChanged(TabPage* page);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);
    void onPagePathChanged(const QString& path);
};
