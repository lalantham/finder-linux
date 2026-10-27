#include "BreadcrumbBar.h"
#include "StyleHelper.h"
#include "ThemeManager.h"
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>

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
        QString target = accumPath;
        connect(btn, &QPushButton::clicked, this, [this, target]() {
            onSegmentClicked(target);
        });
        m_breadcrumbLayout->addWidget(btn);
    }

    m_breadcrumbLayout->addStretch();
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
