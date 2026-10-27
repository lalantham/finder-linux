#include "ThemeManager.h"
#include "StyleHelper.h"
#include <QApplication>
#include <QStyleHints>
#include <QPalette>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

void ThemeManager::applyTheme(AppTheme theme)
{
    if (theme == AppTheme::System) {
        // Detect dark mode from system style hints
        auto* hints = QGuiApplication::styleHints();
        m_isDark = (hints && hints->colorScheme() == Qt::ColorScheme::Dark);
    } else {
        m_isDark = (theme == AppTheme::Dark);
    }

    if (qApp) {
        if (m_isDark) {
            qApp->setStyleSheet(StyleHelper::darkThemeStyle());
        } else {
            qApp->setStyleSheet(StyleHelper::lightThemeStyle());
        }
    }

    emit themeChanged(m_isDark);
}
