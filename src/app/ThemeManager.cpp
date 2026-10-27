#include "ThemeManager.h"
#include "StyleHelper.h"
#include <QApplication>
#include <QStyleHints>
#include <QPalette>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QTextStream>

ThemeManager::ThemeManager()
{
    m_accentColor = detectSystemAccentColor();
    m_accentColorHex = m_accentColor.name();
}

QColor ThemeManager::detectSystemAccentColor()
{
    // 1. Check ~/.config/kdeglobals for KDE Plasma accent/selection background
    QString kdeGlobals = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
    if (QFile::exists(kdeGlobals)) {
        QFile file(kdeGlobals);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            bool inSelection = false;
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inSelection = (line == QStringLiteral("[Colors:Selection]"));
                } else if (inSelection && line.startsWith(QStringLiteral("BackgroundNormal="))) {
                    QString val = line.mid(17).trimmed();
                    QStringList parts = val.split(QLatin1Char(','));
                    if (parts.size() >= 3) {
                        int r = parts[0].toInt();
                        int g = parts[1].toInt();
                        int b = parts[2].toInt();
                        if (r != 0 || g != 0 || b != 0) {
                            return QColor(r, g, b);
                        }
                    }
                }
            }
        }
    }

    // 2. Query QPalette::Accent or QPalette::Highlight from the desktop's system palette
    QPalette sysPal = QGuiApplication::palette();
    QColor col = sysPal.color(QPalette::Accent);
    if (col.isValid() && col != QColor(0, 0, 0) && col != QColor(255, 255, 255) && col != QColor(0x00, 0x7a, 0xff) && col != QColor(0x0a, 0x84, 0xff)) {
        return col;
    }
    col = sysPal.color(QPalette::Highlight);
    if (col.isValid() && col != QColor(0, 0, 0) && col != QColor(255, 255, 255) && col != QColor(0x00, 0x7a, 0xff) && col != QColor(0x0a, 0x84, 0xff)) {
        return col;
    }

    // 3. Fallback: User-provided accent color from system folder icon: #765150
    return QColor(0x76, 0x51, 0x50);
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

void ThemeManager::applyTheme(AppTheme theme)
{
    // Re-detect accent color in case system settings changed
    m_accentColor = detectSystemAccentColor();
    m_accentColorHex = m_accentColor.name();

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
            pal.setColor(QPalette::Link, m_accentColor);
            pal.setColor(QPalette::Highlight, m_accentColor);
            pal.setColor(QPalette::Accent, m_accentColor);
            pal.setColor(QPalette::HighlightedText, Qt::white);
            pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x75, 0x75, 0x78));
            pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x75, 0x75, 0x78));
            pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x75, 0x75, 0x78));

            qApp->setPalette(pal);
            qApp->setStyleSheet(StyleHelper::darkThemeStyle(m_accentColorHex));
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
            pal.setColor(QPalette::Link, m_accentColor);
            pal.setColor(QPalette::Highlight, m_accentColor);
            pal.setColor(QPalette::Accent, m_accentColor);
            pal.setColor(QPalette::HighlightedText, Qt::white);
            pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x8e, 0x8e, 0x93));
            pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x8e, 0x8e, 0x93));
            pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x8e, 0x8e, 0x93));

            qApp->setPalette(pal);
            qApp->setStyleSheet(StyleHelper::lightThemeStyle(m_accentColorHex));
        }
    }

    emit themeChanged(m_isDark);
}
