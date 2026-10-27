#pragma once

#include <QString>
#include <QColor>

class StyleHelper {
public:
    static QString lightThemeStyle();
    static QString darkThemeStyle();

    static QString segmentedButtonLeftStyle(bool isDark = false);
    static QString segmentedButtonMidStyle(bool isDark = false);
    static QString segmentedButtonRightStyle(bool isDark = false);
    static QString segmentedButtonSingleStyle(bool isDark = false);

    static QString searchBarStyle(bool isDark = false);
    static QString breadcrumbButtonStyle(bool isDark = false);
    static QString sidebarStyle(bool isDark = false);
};
