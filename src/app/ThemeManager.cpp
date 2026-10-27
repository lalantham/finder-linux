#include "ThemeManager.h"
#include "StyleHelper.h"
#include <QApplication>
#include <QStyleHints>
#include <QPalette>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QMap>

ThemeManager::ThemeManager()
{
    m_colors = detectColors(AppTheme::System);
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

bool ThemeManager::parseKdeGlobals(ThemeColors& outColors)
{
    QString configPath = QDir::homePath() + QStringLiteral("/.config/kdeglobals");
    if (!QFile::exists(configPath)) {
        return false;
    }
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    auto parseRgb = [](const QString& str) -> QColor {
        QStringList parts = str.split(QLatin1Char(','));
        if (parts.size() >= 3) {
            int r = parts[0].trimmed().toInt();
            int g = parts[1].trimmed().toInt();
            int b = parts[2].trimmed().toInt();
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                return QColor(r, g, b);
            }
        }
        return {};
    };

    QTextStream in(&file);
    QString currentSection;
    QMap<QString, QMap<QString, QString>> ini;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            currentSection = line.mid(1, line.length() - 2);
            continue;
        }
        qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq > 0 && !currentSection.isEmpty()) {
            QString key = line.left(eq).trimmed();
            QString val = line.mid(eq + 1).trimmed();
            ini[currentSection][key] = val;
        }
    }

    // 1. Accent Color
    QColor accent;
    if (ini.contains(QStringLiteral("General"))) {
        if (ini[QStringLiteral("General")].contains(QStringLiteral("AccentColor"))) {
            accent = parseRgb(ini[QStringLiteral("General")][QStringLiteral("AccentColor")]);
        }
        if (!accent.isValid() && ini[QStringLiteral("General")].contains(QStringLiteral("LastUsedCustomAccentColor"))) {
            accent = parseRgb(ini[QStringLiteral("General")][QStringLiteral("LastUsedCustomAccentColor")]);
        }
    }
    if (!accent.isValid() && ini.contains(QStringLiteral("Colors:Selection"))) {
        accent = parseRgb(ini[QStringLiteral("Colors:Selection")][QStringLiteral("DecorationFocus")]);
        if (!accent.isValid()) {
            accent = parseRgb(ini[QStringLiteral("Colors:Selection")][QStringLiteral("BackgroundNormal")]);
        }
    }

    // 2. Window Colors
    QColor window, windowAlt, text, subText;
    if (ini.contains(QStringLiteral("Colors:Window"))) {
        window = parseRgb(ini[QStringLiteral("Colors:Window")][QStringLiteral("BackgroundNormal")]);
        windowAlt = parseRgb(ini[QStringLiteral("Colors:Window")][QStringLiteral("BackgroundAlternate")]);
        text = parseRgb(ini[QStringLiteral("Colors:Window")][QStringLiteral("ForegroundNormal")]);
        subText = parseRgb(ini[QStringLiteral("Colors:Window")][QStringLiteral("ForegroundInactive")]);
    }

    // Header background (toolbar/menubar)
    if (ini.contains(QStringLiteral("Colors:Header"))) {
        QColor headerBg = parseRgb(ini[QStringLiteral("Colors:Header")][QStringLiteral("BackgroundNormal")]);
        if (headerBg.isValid()) {
            windowAlt = headerBg;
        }
    }

    // 3. View Colors (Lists, Icons, Columns)
    QColor view, viewAlt;
    if (ini.contains(QStringLiteral("Colors:View"))) {
        view = parseRgb(ini[QStringLiteral("Colors:View")][QStringLiteral("BackgroundNormal")]);
        viewAlt = parseRgb(ini[QStringLiteral("Colors:View")][QStringLiteral("BackgroundAlternate")]);
    }

    // 4. Button Colors
    QColor button, buttonText;
    if (ini.contains(QStringLiteral("Colors:Button"))) {
        button = parseRgb(ini[QStringLiteral("Colors:Button")][QStringLiteral("BackgroundNormal")]);
        buttonText = parseRgb(ini[QStringLiteral("Colors:Button")][QStringLiteral("ForegroundNormal")]);
    }

    // 5. Selection Colors
    QColor selection, selectionText;
    if (ini.contains(QStringLiteral("Colors:Selection"))) {
        selection = parseRgb(ini[QStringLiteral("Colors:Selection")][QStringLiteral("BackgroundNormal")]);
        selectionText = parseRgb(ini[QStringLiteral("Colors:Selection")][QStringLiteral("ForegroundNormal")]);
    }

    if (!window.isValid()) {
        return false;
    }

    outColors.window = window;
    outColors.windowAlt = windowAlt.isValid() ? windowAlt : (window.lightness() < 128 ? window.lighter(115) : window.darker(108));
    outColors.view = view.isValid() ? view : window;
    outColors.viewAlt = viewAlt.isValid() ? viewAlt : (outColors.view.lightness() < 128 ? outColors.view.lighter(115) : outColors.view.darker(105));
    outColors.text = text.isValid() ? text : (window.lightness() < 128 ? QColor(0xfc, 0xfc, 0xfc) : QColor(0x31, 0x36, 0x3b));
    outColors.subText = subText.isValid() ? subText : (window.lightness() < 128 ? QColor(0xa1, 0xa9, 0xb1) : QColor(0x7f, 0x8c, 0x8d));
    outColors.button = button.isValid() ? button : outColors.windowAlt;
    outColors.buttonText = buttonText.isValid() ? buttonText : outColors.text;
    outColors.accent = accent.isValid() ? accent : QColor(0xad, 0x70, 0x6f);
    outColors.selection = outColors.accent; // In Breeze, selection uses the accent color
    outColors.selectionText = selectionText.isValid() ? selectionText : Qt::white;

    bool dark = (outColors.window.lightness() < 128);
    outColors.isDark = dark;
    outColors.border = dark ? QColor(49, 54, 59) : QColor(211, 215, 217);

    return true;
}

ThemeColors ThemeManager::detectColors(AppTheme theme)
{
    ThemeColors c;
    bool hasKde = parseKdeGlobals(c);

    bool wantDark = false;
    if (theme == AppTheme::System) {
        if (hasKde) {
            wantDark = c.isDark;
        } else {
            auto* hints = QGuiApplication::styleHints();
            if (hints) {
                wantDark = (hints->colorScheme() == Qt::ColorScheme::Dark);
            } else {
                wantDark = (QGuiApplication::palette().color(QPalette::Window).lightness() < 128);
            }
        }
    } else {
        wantDark = (theme == AppTheme::Dark);
    }

    if (hasKde && c.isDark == wantDark) {
        return c;
    }

    // Standard Breeze Dark / Breeze Light palette
    c.isDark = wantDark;
    if (wantDark) {
        c.window = QColor(32, 35, 38);      // #202326 (Breeze Dark Window)
        c.windowAlt = QColor(41, 44, 48);   // #292c30 (Breeze Dark Header/Toolbar)
        c.view = QColor(20, 22, 24);        // #141618 (Breeze Dark View)
        c.viewAlt = QColor(29, 31, 34);     // #1d1f22
        c.text = QColor(252, 252, 252);     // #fcfcfc
        c.subText = QColor(161, 169, 177);  // #a1a9b1
        c.button = QColor(41, 44, 48);      // #292c30
        c.buttonText = QColor(252, 252, 252);
        c.border = QColor(49, 54, 59);      // #31363b
        if (!c.accent.isValid()) c.accent = QColor(173, 112, 111); // #ad706f
        c.selection = c.accent;
        c.selectionText = Qt::white;
    } else {
        c.window = QColor(239, 240, 241);   // #eff0f1 (Breeze Light Window)
        c.windowAlt = QColor(227, 229, 231);// #e3e5e7 (Breeze Light Toolbar)
        c.view = QColor(255, 255, 255);     // #ffffff
        c.viewAlt = QColor(247, 247, 247);  // #f7f7f7
        c.text = QColor(49, 54, 59);        // #31363b
        c.subText = QColor(127, 140, 141);  // #7f8c8d
        c.button = QColor(247, 247, 247);
        c.buttonText = QColor(49, 54, 59);
        c.border = QColor(211, 215, 217);   // #d3d7d9
        if (!c.accent.isValid()) c.accent = QColor(173, 112, 111);
        c.selection = c.accent;
        c.selectionText = Qt::white;
    }

    return c;
}

void ThemeManager::applyTheme(AppTheme theme)
{
    m_colors = detectColors(theme);

    if (qApp) {
        QPalette pal;
        pal.setColor(QPalette::Window, m_colors.window);
        pal.setColor(QPalette::WindowText, m_colors.text);
        pal.setColor(QPalette::Base, m_colors.view);
        pal.setColor(QPalette::AlternateBase, m_colors.viewAlt);
        pal.setColor(QPalette::ToolTipBase, m_colors.windowAlt);
        pal.setColor(QPalette::ToolTipText, m_colors.text);
        pal.setColor(QPalette::Text, m_colors.text);
        pal.setColor(QPalette::Button, m_colors.button);
        pal.setColor(QPalette::ButtonText, m_colors.buttonText);
        pal.setColor(QPalette::BrightText, Qt::white);
        pal.setColor(QPalette::Link, m_colors.accent);
        pal.setColor(QPalette::Highlight, m_colors.accent);
        pal.setColor(QPalette::Accent, m_colors.accent);
        pal.setColor(QPalette::HighlightedText, m_colors.selectionText);
        pal.setColor(QPalette::Disabled, QPalette::Text, m_colors.subText);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, m_colors.subText);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, m_colors.subText);

        qApp->setPalette(pal);
        qApp->setStyleSheet(m_colors.isDark ? StyleHelper::darkThemeStyle(m_colors) : StyleHelper::lightThemeStyle(m_colors));
    }

    emit themeChanged(m_colors.isDark);
}
