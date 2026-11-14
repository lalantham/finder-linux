#include "TearOffTabBar.h"

#include <QApplication>
#include <QMouseEvent>

TearOffTabBar::TearOffTabBar(QWidget* parent)
    : QTabBar(parent)
{
}

void TearOffTabBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressedIndex = tabAt(event->position().toPoint());
        m_pressedTabData = m_pressedIndex >= 0 ? tabData(m_pressedIndex) : QVariant();
        m_pressGlobalPosition = event->globalPosition().toPoint();
        m_tearOffStarted = false;
    }
    QTabBar::mousePressEvent(event);
}

void TearOffTabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_tearOffStarted && m_pressedIndex >= 0 && (event->buttons() & Qt::LeftButton)) {
        const QPoint globalPosition = event->globalPosition().toPoint();
        const int distance = (globalPosition - m_pressGlobalPosition).manhattanLength();
        const QPoint localPosition = event->position().toPoint();

        // Only vertical drags out of the tab strip tear the tab off. Horizontal
        // drags within the strip continue to reorder tabs normally.
        if (distance >= QApplication::startDragDistance()
            && (localPosition.y() < -8 || localPosition.y() > height() + 8)) {
            m_tearOffStarted = true;

            int index = m_pressedIndex;
            if (m_pressedTabData.isValid()) {
                for (int i = 0; i < count(); ++i) {
                    if (tabData(i) == m_pressedTabData) {
                        index = i;
                        break;
                    }
                }
            }

            m_pressedIndex = -1;
            emit tearOffRequested(index);
            event->accept();
            return;
        }
    }

    QTabBar::mouseMoveEvent(event);
}

void TearOffTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    m_pressedIndex = -1;
    m_pressedTabData.clear();
    m_tearOffStarted = false;
    QTabBar::mouseReleaseEvent(event);
}
