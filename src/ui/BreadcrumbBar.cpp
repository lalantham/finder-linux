#include "BreadcrumbBar.h"
#include "StyleHelper.h"
#include "ThemeManager.h"
#include "AppSettings.h"
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>

BreadcrumbBar::BreadcrumbBar(QWidget* parent)
    : QWidget(parent)
{
    m_stackLayout = new QStackedLayout(this);
    m_stackLayout->setContentsMargins(0, 0, 0, 0);

    m_breadcrumbContainer = new QWidget(this);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbContainer);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(2);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setStyleSheet(StyleHelper::searchBarStyle(ThemeManager::instance().isDark()));
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &BreadcrumbBar::onPathEditFinished);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &BreadcrumbBar::onSpringTimeout);

    m_stackLayout->addWidget(m_breadcrumbContainer);
    m_stackLayout->addWidget(m_pathEdit);
}

void BreadcrumbBar::updateThemeStyles(bool isDark)
{
    if (m_pathEdit) {
        m_pathEdit->setStyleSheet(StyleHelper::searchBarStyle(isDark));
    }
    rebuildBreadcrumbs();
}

void BreadcrumbBar::setPath(const QString& path)
{
    m_currentPath = QDir::cleanPath(path);
    m_pathEdit->setText(m_currentPath);
    rebuildBreadcrumbs();
    m_stackLayout->setCurrentWidget(m_breadcrumbContainer);
}

void BreadcrumbBar::rebuildBreadcrumbs()
{
    // Clear old breadcrumbs
    QLayoutItem* item;
    while ((item = m_breadcrumbLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    if (m_currentPath.isEmpty()) {
        return;
    }

    QStringList segments = m_currentPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    bool isDark = ThemeManager::instance().isDark();

    // Root button
    auto* rootBtn = new QPushButton(QStringLiteral("/"), m_breadcrumbContainer);
    rootBtn->setStyleSheet(StyleHelper::breadcrumbButtonStyle(isDark));
    rootBtn->setAcceptDrops(true);
    rootBtn->setProperty("crumbPath", QStringLiteral("/"));
    rootBtn->installEventFilter(this);
    connect(rootBtn, &QPushButton::clicked, this, [this]() {
        onSegmentClicked(QStringLiteral("/"));
    });
    m_breadcrumbLayout->addWidget(rootBtn);

    QString accumPath = QStringLiteral("");
    for (int i = 0; i < segments.size(); ++i) {
        accumPath += QStringLiteral("/") + segments.at(i);

        auto* separator = new QLabel(QStringLiteral("›"), m_breadcrumbContainer);
        separator->setStyleSheet(isDark ? QStringLiteral("color: #636366; font-size: 11px;")
                                        : QStringLiteral("color: #8e8e93; font-size: 11px;"));
        m_breadcrumbLayout->addWidget(separator);

        auto* btn = new QPushButton(segments.at(i), m_breadcrumbContainer);
        btn->setStyleSheet(StyleHelper::breadcrumbButtonStyle(isDark));
        btn->setAcceptDrops(true);
        QString target = accumPath;
        btn->setProperty("crumbPath", target);
        btn->installEventFilter(this);
        connect(btn, &QPushButton::clicked, this, [this, target]() {
            onSegmentClicked(target);
        });
        m_breadcrumbLayout->addWidget(btn);
    }

    m_breadcrumbLayout->addStretch();
}

bool BreadcrumbBar::eventFilter(QObject* watched, QEvent* event)
{
    if (!AppSettings::instance().springLoadedFoldersEnabled()) {
        return QWidget::eventFilter(watched, event);
    }

    auto* btn = qobject_cast<QPushButton*>(watched);
    if (!btn || !btn->property("crumbPath").isValid()) {
        return QWidget::eventFilter(watched, event);
    }

    QString crumbPath = btn->property("crumbPath").toString();

    if (event->type() == QEvent::DragEnter) {
        auto* dragEvent = static_cast<QDragEnterEvent*>(event);
        if (dragEvent->mimeData()->hasUrls()) {
            bool safe = true;
            for (const auto& url : dragEvent->mimeData()->urls()) {
                if (url.isLocalFile()) {
                    QString src = url.toLocalFile();
                    if (crumbPath == src || crumbPath.startsWith(src + QLatin1Char('/'))) {
                        safe = false;
                        break;
                    }
                }
            }
            if (safe) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
    } else if (event->type() == QEvent::DragMove) {
        auto* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (dragEvent->mimeData()->hasUrls()) {
            if (m_springHoverPath != crumbPath) {
                resetSpringTimer();
                m_springHoverPath = crumbPath;
                m_springHoverWidget = btn;
                bool isDark = ThemeManager::instance().isDark();
                QColor acc = ThemeManager::instance().accentColor();
                btn->setStyleSheet(StyleHelper::breadcrumbButtonStyle(isDark) +
                    QStringLiteral("QPushButton { border: 1.5px solid %1; background: rgba(%2,%3,%4,0.22); }")
                    .arg(acc.name()).arg(acc.red()).arg(acc.green()).arg(acc.blue()));
                m_springTimer->start(AppSettings::instance().springLoadDelayMs());
            }
            dragEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::DragLeave) {
        resetSpringTimer();
    } else if (event->type() == QEvent::Drop) {
        resetSpringTimer();
    }

    return QWidget::eventFilter(watched, event);
}

void BreadcrumbBar::resetSpringTimer()
{
    if (m_springTimer && m_springTimer->isActive()) {
        m_springTimer->stop();
    }
    if (m_springHoverWidget) {
        bool isDark = ThemeManager::instance().isDark();
        m_springHoverWidget->setStyleSheet(StyleHelper::breadcrumbButtonStyle(isDark));
        m_springHoverWidget = nullptr;
    }
    m_springHoverPath.clear();
}

void BreadcrumbBar::onSpringTimeout()
{
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        onSegmentClicked(target);
    }
}

void BreadcrumbBar::onSegmentClicked(const QString& path)
{
    setPath(path);
    emit pathSelected(path);
}

void BreadcrumbBar::onPathEditFinished()
{
    QString entered = m_pathEdit->text().trimmed();
    if (QDir(entered).exists()) {
        setPath(entered);
        emit pathSelected(entered);
    }
    m_stackLayout->setCurrentWidget(m_breadcrumbContainer);
}

void BreadcrumbBar::showPathEditor()
{
    m_pathEdit->setText(m_currentPath);
    m_stackLayout->setCurrentWidget(m_pathEdit);
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}
