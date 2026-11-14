#pragma once

#include <QTabBar>
#include <QVariant>

// QTabBar detects when a tab is dragged vertically out of the tab strip and
// asks TabManager to move that tab into a new window.
class TearOffTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit TearOffTabBar(QWidget* parent = nullptr);

signals:
    void tearOffRequested(int index);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int m_pressedIndex{-1};
    QVariant m_pressedTabData;
    QPoint m_pressGlobalPosition;
    bool m_tearOffStarted{false};
};
