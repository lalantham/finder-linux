#include "BreadcrumbBar.h"
#include "AppSettings.h"
#include "StyleHelper.h"
#include "TagManager.h"
#include "ThemeManager.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

BreadcrumbBar::BreadcrumbBar(QWidget* parent) : QWidget(parent) {
    m_stackLayout = new QStackedLayout(this);
    m_stackLayout->setContentsMargins(0, 0, 0, 0);

    m_breadcrumbContainer = new QWidget(this);
    m_breadcrumbContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_breadcrumbContainer->installEventFilter(this);
    m_breadcrumbLayout = new QHBoxLayout(m_breadcrumbContainer);
    m_breadcrumbLayout->setContentsMargins(0, 0, 0, 0);
    m_breadcrumbLayout->setSpacing(2);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(QStringLiteral("Click the empty area to edit the path, right-click to copy it"));

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setStyleSheet(StyleHelper::searchBarStyle(ThemeManager::instance().isDark()));
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &BreadcrumbBar::onPathEditFinished);

    m_completerModel = new QFileSystemModel(this);
    m_completerModel->setRootPath(QStringLiteral("/"));
    m_completerModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Hidden);

    m_completer = new QCompleter(m_completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_pathEdit->setCompleter(m_completer);
    m_pathEdit->installEventFilter(this);

    m_springTimer = new QTimer(this);
    m_springTimer->setSingleShot(true);
    connect(m_springTimer, &QTimer::timeout, this, &BreadcrumbBar::onSpringTimeout);

    m_stackLayout->addWidget(m_breadcrumbContainer);
    m_stackLayout->addWidget(m_pathEdit);
}

void BreadcrumbBar::updateThemeStyles(bool isDark) {
    if (m_pathEdit) {
        m_pathEdit->setStyleSheet(StyleHelper::searchBarStyle(isDark));
    }
    rebuildBreadcrumbs();
}

void BreadcrumbBar::setPath(const QString& path) {
    if (path.startsWith(QStringLiteral("tag://")) || path.startsWith(QStringLiteral("smart://")) ||
        path.startsWith(QStringLiteral("recent://")) ||
        path.startsWith(QStringLiteral("apps://"))) {
        m_currentPath = path;
    } else {
        m_currentPath = QDir::cleanPath(path);
    }
    m_pathEdit->setText(m_currentPath);
    rebuildBreadcrumbs();
    m_stackLayout->setCurrentWidget(m_breadcrumbContainer);
}

void BreadcrumbBar::rebuildBreadcrumbs() {
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

    bool isDark = ThemeManager::instance().isDark();

    if (m_currentPath.startsWith(QStringLiteral("recent://"))) {
        auto* label = new QLabel(QStringLiteral("Recents"), m_breadcrumbContainer);
        label->setStyleSheet(
            isDark ? QStringLiteral("color: #f0f0f0; font-weight: bold; padding: 2px 6px;")
                   : QStringLiteral("color: #1d1d1f; font-weight: bold; padding: 2px 6px;"));
        m_breadcrumbLayout->addWidget(label);
        m_breadcrumbLayout->addStretch();
        return;
    }

    if (m_currentPath.startsWith(QStringLiteral("apps://"))) {
        auto* label = new QLabel(QStringLiteral("Applications"), m_breadcrumbContainer);
        label->setStyleSheet(
            isDark ? QStringLiteral("color: #f0f0f0; font-weight: bold; padding: 2px 6px;")
                   : QStringLiteral("color: #1d1d1f; font-weight: bold; padding: 2px 6px;"));
        m_breadcrumbLayout->addWidget(label);
        m_breadcrumbLayout->addStretch();
        return;
    }

    if (m_currentPath.startsWith(QStringLiteral("smart://"))) {
        auto* header = new QLabel(QStringLiteral("Smart Folders"), m_breadcrumbContainer);
        header->setStyleSheet(
            isDark ? QStringLiteral("color: #8e8e93; font-weight: bold; padding: 2px 6px;")
                   : QStringLiteral("color: #636366; font-weight: bold; padding: 2px 6px;"));
        m_breadcrumbLayout->addWidget(header);
        auto* separator = new QLabel(QStringLiteral("›"), m_breadcrumbContainer);
        separator->setStyleSheet(isDark ? QStringLiteral("color: #636366;")
                                        : QStringLiteral("color: #8e8e93;"));
        m_breadcrumbLayout->addWidget(separator);
        auto* label = new QLabel(m_currentPath.mid(8), m_breadcrumbContainer);
        label->setStyleSheet(
            isDark ? QStringLiteral("color: #f0f0f0; font-weight: bold; padding: 2px 6px;")
                   : QStringLiteral("color: #1d1d1f; font-weight: bold; padding: 2px 6px;"));
        m_breadcrumbLayout->addWidget(label);
        m_breadcrumbLayout->addStretch();
        return;
    }

    if (m_currentPath.startsWith(QStringLiteral("tag://"))) {
        QString tagName = m_currentPath.mid(6);
        auto* tagsLabel = new QLabel(QStringLiteral("Tags"), m_breadcrumbContainer);
        tagsLabel->setStyleSheet(
            isDark ? QStringLiteral("color: #8e8e93; font-weight: bold; padding: 2px 6px;")
                   : QStringLiteral("color: #636366; font-weight: bold; padding: 2px 6px;"));
        m_breadcrumbLayout->addWidget(tagsLabel);

        auto* separator = new QLabel(QStringLiteral("›"), m_breadcrumbContainer);
        separator->setStyleSheet(isDark ? QStringLiteral("color: #636366; font-size: 11px;")
                                        : QStringLiteral("color: #8e8e93; font-size: 11px;"));
        m_breadcrumbLayout->addWidget(separator);

        auto* tagBtn = new QPushButton(m_breadcrumbContainer);
        tagBtn->setText(QStringLiteral(" ") + tagName);
        tagBtn->setIcon(TagManager::tagDotIcon(tagName, 14));
        tagBtn->setStyleSheet(StyleHelper::breadcrumbButtonStyle(isDark));
        m_breadcrumbLayout->addWidget(tagBtn);
        m_breadcrumbLayout->addStretch();
        return;
    }

    QStringList segments = m_currentPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    // Root button
    auto* rootBtn = new QPushButton(QStringLiteral("/"), m_breadcrumbContainer);
    rootBtn->setStyleSheet(StyleHelper::breadcrumbButtonStyle(isDark));
    rootBtn->setAcceptDrops(true);
    rootBtn->setProperty("crumbPath", QStringLiteral("/"));
    rootBtn->installEventFilter(this);
    if (segments.isEmpty()) {
        rootBtn->setToolTip(QStringLiteral("Click to edit the path"));
        connect(rootBtn, &QPushButton::clicked, this, [this]() { showPathEditor(); });
    } else {
        connect(rootBtn, &QPushButton::clicked, this,
                [this]() { onSegmentClicked(QStringLiteral("/")); });
    }
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
        if (i == segments.size() - 1) {
            // Clicking the current folder opens the path editor instead of
            // navigating to where we already are.
            btn->setToolTip(QStringLiteral("Click to edit the path"));
            connect(btn, &QPushButton::clicked, this, [this]() { showPathEditor(); });
        } else {
            connect(btn, &QPushButton::clicked, this,
                    [this, target]() { onSegmentClicked(target); });
        }
        m_breadcrumbLayout->addWidget(btn);
    }

    m_breadcrumbLayout->addStretch();
}

bool BreadcrumbBar::eventFilter(QObject* watched, QEvent* event) {
    // Escape cancels path editing.
    if (watched == m_pathEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            m_pathEdit->setText(m_currentPath);
            m_stackLayout->setCurrentWidget(m_breadcrumbContainer);
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    // Clicking anywhere outside the editor (a file, a folder, the sidebar,
    // the toolbar...) folds it back into breadcrumbs. Defer one event-loop
    // turn so clicking an autocomplete entry does not close the editor.
    if (watched == m_pathEdit && event->type() == QEvent::FocusOut) {
        QTimer::singleShot(0, this, [this]() {
            if (m_stackLayout->currentWidget() != m_pathEdit) {
                return;
            }
            QWidget* focus = QApplication::focusWidget();
            QWidget* popup = m_completer ? m_completer->popup() : nullptr;
            if (focus == m_pathEdit) {
                return;
            }
            if (popup &&
                (popup->isVisible() || (focus && (focus == popup || popup->isAncestorOf(focus))))) {
                return;
            }
            m_pathEdit->setText(m_currentPath);
            m_stackLayout->setCurrentWidget(m_breadcrumbContainer);
        });
        return false;
    }

    // Clicking the empty part of the bar opens the editor with the path
    // selected, ready to edit or copy. Right-click offers the same.
    if (watched == m_breadcrumbContainer) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QWidget* child = m_breadcrumbContainer->childAt(mouseEvent->position().toPoint());
                if (!qobject_cast<QPushButton*>(child)) {
                    showPathEditor();
                    return true;
                }
            }
        } else if (event->type() == QEvent::ContextMenu) {
            auto* contextEvent = static_cast<QContextMenuEvent*>(event);
            showContextMenu(contextEvent->globalPos());
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    auto* btn = qobject_cast<QPushButton*>(watched);
    if (btn && btn->property("crumbPath").isValid() && event->type() == QEvent::ContextMenu) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        showContextMenu(contextEvent->globalPos());
        return true;
    }

    if (!AppSettings::instance().springLoadedFoldersEnabled()) {
        return QWidget::eventFilter(watched, event);
    }

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
                btn->setStyleSheet(
                    StyleHelper::breadcrumbButtonStyle(isDark) +
                    QStringLiteral(
                        "QPushButton { border: 1.5px solid %1; background: rgba(%2,%3,%4,0.22); }")
                        .arg(acc.name())
                        .arg(acc.red())
                        .arg(acc.green())
                        .arg(acc.blue()));
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

void BreadcrumbBar::showContextMenu(const QPoint& globalPos) {
    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("document-edit")), QStringLiteral("Edit Path"),
                   this, [this]() { showPathEditor(); });
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy Path"), this,
                   [this]() { QApplication::clipboard()->setText(m_currentPath); });
    menu.exec(globalPos);
}

void BreadcrumbBar::resetSpringTimer() {
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

void BreadcrumbBar::onSpringTimeout() {
    if (!m_springHoverPath.isEmpty()) {
        QString target = m_springHoverPath;
        resetSpringTimer();
        onSegmentClicked(target);
    }
}

void BreadcrumbBar::onSegmentClicked(const QString& path) {
    setPath(path);
    emit pathSelected(path);
}

void BreadcrumbBar::onPathEditFinished() {
    QString entered = m_pathEdit->text().trimmed();
    if (entered.startsWith(QStringLiteral("file://"))) {
        entered = QUrl(entered).toLocalFile();
    }
    if (entered == QLatin1Char('~')) {
        entered = QDir::homePath();
    } else if (entered.startsWith(QStringLiteral("~/"))) {
        entered = QDir::homePath() + entered.mid(1);
    }
    entered = QDir::cleanPath(entered);

    QFileInfo fi(entered);
    if (fi.exists()) {
        QString dirPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        setPath(dirPath);
        emit pathSelected(dirPath);
    }
    m_stackLayout->setCurrentWidget(m_breadcrumbContainer);
}

void BreadcrumbBar::showPathEditor() {
    m_pathEdit->setText(m_currentPath);
    m_stackLayout->setCurrentWidget(m_pathEdit);
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}
