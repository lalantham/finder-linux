#pragma once

#include <QString>
#include <QColor>

struct ThemeColors;

class StyleHelper {
public:
    static QString lightThemeStyle(const ThemeColors& colors);
    static QString lightThemeStyle(const QString& accentColor = QString());
    static QString darkThemeStyle(const ThemeColors& colors);
    static QString darkThemeStyle(const QString& accentColor = QString());

    static QString segmentedButtonLeftStyle(bool isDark = false, const QString& accentColor = QString());
    static QString segmentedButtonMidStyle(bool isDark = false, const QString& accentColor = QString());
    static QString segmentedButtonRightStyle(bool isDark = false, const QString& accentColor = QString());
    static QString segmentedButtonSingleStyle(bool isDark = false, const QString& accentColor = QString());

    static QString searchBarStyle(bool isDark = false, const QString& accentColor = QString());
    static QString breadcrumbButtonStyle(bool isDark = false);
    static QString sidebarStyle(bool isDark = false, const QString& accentColor = QString());
};
