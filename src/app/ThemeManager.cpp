#include "ThemeManager.h"
#include "StyleHelper.h"
#include <QApplication>
#include <QStyleHints>
#include <QPalette>
#include <QColor>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

void ThemeManager::applyTheme(AppTheme theme)
{
    if (theme == AppTheme::System) {
        auto* hints = QGuiApplication::styleHints();
        if (hints) {
            m_isDark = (hints->colorScheme() == Qt::ColorScheme::Dark);
        } else {
            // Fallback check
            QPalette defaultPal = QGuiApplication::palette();
            m_isDark = (defaultPal.color(QPalette::Window).lightness() < 128);
        }
    } else {
        m_isDark = (theme == AppTheme::Dark);
    }

    if (qApp) {
        QPalette pal;
        if (m_isDark) {
            pal.setColor(QPalette::Window, QColor(0x1e, 0x1e, 0x1f));
            pal.setColor(QPalette::WindowText, QColor(0xf5, 0xf5, 0xf7));
            pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
            pal.setColor(QPalette::AlternateBase, QColor(0x28, 0x28, 0x2a));
            pal.setColor(QPalette::ToolTipBase, QColor(0x2d, 0x2d, 0x30));
            pal.setColor(QPalette::ToolTipText, QColor(0xf5, 0xf5, 0xf7));
            pal.setColor(QPalette::Text, QColor(0xf5, 0xf5, 0xf7));
            pal.setColor(QPalette::Button, QColor(0x38, 0x38, 0x3a));
            pal.setColor(QPalette::ButtonText, QColor(0xf5, 0xf5, 0xf7));
            pal.setColor(QPalette::BrightText, Qt::white);
            pal.setColor(QPalette::Link, QColor(0x0a, 0x84, 0xff));
            pal.setColor(QPalette::Highlight, QColor(0x0a, 0x84, 0xff));
            pal.setColor(QPalette::HighlightedText, Qt::white);
            pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x75, 0x75, 0x78));
            pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x75, 0x75, 0x78));
            pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x75, 0x75, 0x78));

            qApp->setPalette(pal);
            qApp->setStyleSheet(StyleHelper::darkThemeStyle());
        } else {
            pal.setColor(QPalette::Window, QColor(0xf6, 0xf6, 0xf6));
            pal.setColor(QPalette::WindowText, QColor(0x1d, 0x1d, 0x1f));
            pal.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
            pal.setColor(QPalette::AlternateBase, QColor(0xf9, 0xf9, 0xf9));
            pal.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
            pal.setColor(QPalette::ToolTipText, QColor(0x1d, 0x1d, 0x1f));
            pal.setColor(QPalette::Text, QColor(0x1d, 0x1d, 0x1f));
            pal.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
            pal.setColor(QPalette::ButtonText, QColor(0x1d, 0x1d, 0x1f));
            pal.setColor(QPalette::BrightText, Qt::white);
            pal.setColor(QPalette::Link, QColor(0x00, 0x7a, 0xff));
            pal.setColor(QPalette::Highlight, QColor(0x00, 0x7a, 0xff));
            pal.setColor(QPalette::HighlightedText, Qt::white);
            pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x8e, 0x8e, 0x93));
            pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x8e, 0x8e, 0x93));
            pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x8e, 0x8e, 0x93));

            qApp->setPalette(pal);
            qApp->setStyleSheet(StyleHelper::lightThemeStyle());
        }
    }

    emit themeChanged(m_isDark);
}
