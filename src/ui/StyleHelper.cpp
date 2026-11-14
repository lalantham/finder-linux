#include "StyleHelper.h"
#include "ThemeManager.h"

QString StyleHelper::lightThemeStyle(const ThemeColors& c) {
    QString windowHex = c.window.name();
    QString windowAltHex = c.windowAlt.name();
    QString viewHex = c.view.name();
    QString viewAltHex = c.viewAlt.name();
    QString textHex = c.text.name();
    QString subTextHex = c.subText.name();
    QString borderHex = c.border.name();
    QString accentHex = c.accent.name();
    QString selTextHex = c.selectionText.name();
    const QString navHover = QStringLiteral("rgba(0, 0, 0, 0.06)");

    return QStringLiteral("QWidget {"
                          "  color: %1;"
                          "}"
                          "QMainWindow {"
                          "  background-color: %2;"
                          "}"
                          "QMenuBar {"
                          "  background-color: %3;"
                          "  color: %1;"
                          "  border-bottom: none;"
                          "  padding: 2px 8px;"
                          "  font-size: 13px;"
                          "}"
                          "QMenuBar::item {"
                          "  background: transparent;"
                          "  color: %1;"
                          "  padding: 4px 8px;"
                          "  border-radius: 4px;"
                          "}"
                          "QMenuBar::item:selected {"
                          "  background-color: rgba(0, 0, 0, 0.08);"
                          "}"
                          "QMenuBar::item:pressed {"
                          "  background-color: %5;"
                          "  color: #ffffff;"
                          "}"
                          "QToolBar {"
                          "  background-color: %2;"
                          "  border: none;"
                          "  spacing: 8px;"
                          "  padding: 10px 14px;"
                          "}"
                          "QWidget#pathCapsule {"
                          "  background-color: %3;"
                          "  border: 1px solid %4;"
                          "  border-radius: 16px;"
                          "}"
                          "QWidget#pathCapsule QToolButton {"
                          "  background: transparent;"
                          "  color: %1;"
                          "  border: none;"
                          "  border-radius: 10px;"
                          "  padding: 4px 8px;"
                          "}"
                          "QWidget#pathCapsule QToolButton:hover {"
                          "  background-color: %10;"
                          "}"
                          "QWidget#pathCapsule QToolButton:disabled {"
                          "  color: %9;"
                          "}"
                          "QSplitter::handle {"
                          "  background-color: %4;"
                          "  width: 1px;"
                          "}"
                          "QTreeWidget#sidebar {"
                          "  background-color: %3;"
                          "  border: 1px solid %4;"
                          "  border-radius: 12px;"
                          "  margin: 6px;"
                          "  padding: 6px;"
                          "  color: %1;"
                          "  font-size: 12px;"
                          "}"
                          "QTreeWidget#sidebar::item {"
                          "  color: %1;"
                          "  padding: 5px 8px;"
                          "  border-radius: 7px;"
                          "  margin: 1px 6px;"
                          "}"
                          "QTreeWidget#sidebar::item:hover {"
                          "  background-color: rgba(0, 0, 0, 0.05);"
                          "}"
                          "QTreeWidget#sidebar::item:selected {"
                          "  background-color: %5;"
                          "  color: %6;"
                          "  font-weight: 500;"
                          "}"
                          "QTreeView, QListView, QTableView {"
                          "  background-color: %7;"
                          "  alternate-background-color: %8;"
                          "  border: none;"
                          "  color: %1;"
                          "  selection-background-color: %5;"
                          "  selection-color: %6;"
                          "  outline: none;"
                          "  font-size: 13px;"
                          "}"
                          "QHeaderView::section {"
                          "  background-color: %3;"
                          "  color: %9;"
                          "  padding: 4px 8px;"
                          "  border: none;"
                          "  border-right: 1px solid %4;"
                          "  border-bottom: 1px solid %4;"
                          "  font-weight: 500;"
                          "  font-size: 11px;"
                          "}"
                          "QStatusBar {"
                          "  background-color: %3;"
                          "  border-top: 1px solid %4;"
                          "  color: %9;"
                          "  font-size: 11px;"
                          "}"
                          "QStatusBar QLabel {"
                          "  color: %9;"
                          "}"
                          "QTabBar::tab {"
                          "  background: %8;"
                          "  color: %9;"
                          "  border-top-left-radius: 6px;"
                          "  border-top-right-radius: 6px;"
                          "  padding: 6px 14px;"
                          "  margin-right: 2px;"
                          "  font-size: 12px;"
                          "}"
                          "QTabBar::tab:selected {"
                          "  background: %7;"
                          "  color: %1;"
                          "  font-weight: bold;"
                          "}"
                          "QMenu {"
                          "  background-color: %3;"
                          "  color: %1;"
                          "  border: 1px solid %4;"
                          "  border-radius: 9px;"
                          "  padding: 5px;"
                          "}"
                          "QMenu::item {"
                          "  color: %1;"
                          "  padding: 5px 16px 5px 8px;"
                          "  border-radius: 6px;"
                          "  margin: 2px 5px;"
                          "}"
                          "QMenu::item:selected {"
                          "  background-color: %5;"
                          "  color: #ffffff;"
                          "}"
                          "QMenu::item:disabled {"
                          "  color: rgba(0, 0, 0, 0.35);"
                          "  background: transparent;"
                          "}"
                          "QMenu::icon {"
                          "  padding-left: 6px;"
                          "}"
                          "QMenu::separator {"
                          "  height: 1px;"
                          "  background-color: %4;"
                          "  margin: 5px 8px;"
                          "}"
                          "QMenu::right-arrow {"
                          "  margin-right: 8px;"
                          "}"
                          "QScrollBar:vertical {"
                          "  background: transparent;"
                          "  width: 8px;"
                          "  margin: 0px;"
                          "}"
                          "QScrollBar::handle:vertical {"
                          "  background: rgba(0, 0, 0, 0.2);"
                          "  min-height: 20px;"
                          "  border-radius: 4px;"
                          "}"
                          "QScrollBar::handle:vertical:hover {"
                          "  background: rgba(0, 0, 0, 0.4);"
                          "}"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                          "  height: 0px;"
                          "}"
                          "QScrollBar:horizontal {"
                          "  background: transparent;"
                          "  height: 8px;"
                          "  margin: 0px;"
                          "}"
                          "QScrollBar::handle:horizontal {"
                          "  background: rgba(0, 0, 0, 0.2);"
                          "  min-width: 20px;"
                          "  border-radius: 4px;"
                          "}"
                          "QScrollBar::handle:horizontal:hover {"
                          "  background: rgba(0, 0, 0, 0.4);"
                          "}"
                          "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
                          "  width: 0px;"
                          "}")
        .arg(textHex,      // %1
             windowHex,    // %2
             windowAltHex, // %3
             borderHex,    // %4
             accentHex,    // %5
             selTextHex,   // %6
             viewHex,      // %7
             viewAltHex,   // %8
             subTextHex,   // %9
             navHover      // %10
        );
}

QString StyleHelper::lightThemeStyle(const QString& accentColor) {
    ThemeColors c = ThemeManager::instance().colors();
    if (!accentColor.isEmpty()) {
        c.accent = QColor(accentColor);
        c.selection = c.accent;
    }
    return lightThemeStyle(c);
}

QString StyleHelper::darkThemeStyle(const ThemeColors& c) {
    QString windowHex = c.window.name();
    QString windowAltHex = c.windowAlt.name();
    QString viewHex = c.view.name();
    QString viewAltHex = c.viewAlt.name();
    QString textHex = c.text.name();
    QString subTextHex = c.subText.name();
    QString borderHex = c.border.name();
    QString accentHex = c.accent.name();
    QString selTextHex = c.selectionText.name();
    const QString navHover = QStringLiteral("rgba(255, 255, 255, 0.10)");

    return QStringLiteral("QWidget {"
                          "  color: %1;"
                          "}"
                          "QMainWindow {"
                          "  background-color: %2;"
                          "}"
                          "QMenuBar {"
                          "  background-color: %3;"
                          "  color: %1;"
                          "  border-bottom: none;"
                          "  padding: 2px 8px;"
                          "  font-size: 13px;"
                          "}"
                          "QMenuBar::item {"
                          "  background: transparent;"
                          "  color: %1;"
                          "  padding: 4px 8px;"
                          "  border-radius: 4px;"
                          "}"
                          "QMenuBar::item:selected {"
                          "  background-color: rgba(255, 255, 255, 0.12);"
                          "}"
                          "QMenuBar::item:pressed {"
                          "  background-color: %5;"
                          "  color: #ffffff;"
                          "}"
                          "QToolBar {"
                          "  background-color: %2;"
                          "  border: none;"
                          "  spacing: 8px;"
                          "  padding: 10px 14px;"
                          "}"
                          "QWidget#pathCapsule {"
                          "  background-color: %3;"
                          "  border: 1px solid %4;"
                          "  border-radius: 16px;"
                          "}"
                          "QWidget#pathCapsule QToolButton {"
                          "  background: transparent;"
                          "  color: %1;"
                          "  border: none;"
                          "  border-radius: 10px;"
                          "  padding: 4px 8px;"
                          "}"
                          "QWidget#pathCapsule QToolButton:hover {"
                          "  background-color: %10;"
                          "}"
                          "QWidget#pathCapsule QToolButton:disabled {"
                          "  color: %9;"
                          "}"
                          "QSplitter::handle {"
                          "  background-color: %4;"
                          "  width: 1px;"
                          "}"
                          "QTreeWidget#sidebar {"
                          "  background-color: %3;"
                          "  border: 1px solid %4;"
                          "  border-radius: 12px;"
                          "  margin: 6px;"
                          "  padding: 6px;"
                          "  color: %1;"
                          "  font-size: 12px;"
                          "}"
                          "QTreeWidget#sidebar::item {"
                          "  color: %1;"
                          "  padding: 5px 8px;"
                          "  border-radius: 7px;"
                          "  margin: 1px 6px;"
                          "}"
                          "QTreeWidget#sidebar::item:hover {"
                          "  background-color: rgba(255, 255, 255, 0.08);"
                          "}"
                          "QTreeWidget#sidebar::item:selected {"
                          "  background-color: %5;"
                          "  color: %6;"
                          "  font-weight: 500;"
                          "}"
                          "QTreeView, QListView, QTableView {"
                          "  background-color: %7;"
                          "  alternate-background-color: %8;"
                          "  border: none;"
                          "  color: %1;"
                          "  selection-background-color: %5;"
                          "  selection-color: %6;"
                          "  outline: none;"
                          "  font-size: 13px;"
                          "}"
                          "QHeaderView::section {"
                          "  background-color: %3;"
                          "  color: %9;"
                          "  padding: 4px 8px;"
                          "  border: none;"
                          "  border-right: 1px solid %4;"
                          "  border-bottom: 1px solid %4;"
                          "  font-weight: 500;"
                          "  font-size: 11px;"
                          "}"
                          "QStatusBar {"
                          "  background-color: %3;"
                          "  border-top: 1px solid %4;"
                          "  color: %9;"
                          "  font-size: 11px;"
                          "}"
                          "QStatusBar QLabel {"
                          "  color: %9;"
                          "}"
                          "QTabBar::tab {"
                          "  background: %8;"
                          "  color: %9;"
                          "  border-top-left-radius: 6px;"
                          "  border-top-right-radius: 6px;"
                          "  padding: 6px 14px;"
                          "  margin-right: 2px;"
                          "  font-size: 12px;"
                          "}"
                          "QTabBar::tab:selected {"
                          "  background: %7;"
                          "  color: %1;"
                          "  font-weight: bold;"
                          "}"
                          "QMenu {"
                          "  background-color: %3;"
                          "  color: %1;"
                          "  border: 1px solid %4;"
                          "  border-radius: 9px;"
                          "  padding: 5px;"
                          "}"
                          "QMenu::item {"
                          "  color: %1;"
                          "  padding: 5px 16px 5px 8px;"
                          "  border-radius: 6px;"
                          "  margin: 2px 5px;"
                          "}"
                          "QMenu::item:selected {"
                          "  background-color: %5;"
                          "  color: #ffffff;"
                          "}"
                          "QMenu::item:disabled {"
                          "  color: rgba(255, 255, 255, 0.35);"
                          "  background: transparent;"
                          "}"
                          "QMenu::icon {"
                          "  padding-left: 6px;"
                          "}"
                          "QMenu::separator {"
                          "  height: 1px;"
                          "  background-color: %4;"
                          "  margin: 5px 8px;"
                          "}"
                          "QMenu::right-arrow {"
                          "  margin-right: 8px;"
                          "}"
                          "QScrollBar:vertical {"
                          "  background: transparent;"
                          "  width: 8px;"
                          "  margin: 0px;"
                          "}"
                          "QScrollBar::handle:vertical {"
                          "  background: rgba(255, 255, 255, 0.2);"
                          "  min-height: 20px;"
                          "  border-radius: 4px;"
                          "}"
                          "QScrollBar::handle:vertical:hover {"
                          "  background: rgba(255, 255, 255, 0.4);"
                          "}"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                          "  height: 0px;"
                          "}"
                          "QScrollBar:horizontal {"
                          "  background: transparent;"
                          "  height: 8px;"
                          "  margin: 0px;"
                          "}"
                          "QScrollBar::handle:horizontal {"
                          "  background: rgba(255, 255, 255, 0.2);"
                          "  min-width: 20px;"
                          "  border-radius: 4px;"
                          "}"
                          "QScrollBar::handle:horizontal:hover {"
                          "  background: rgba(255, 255, 255, 0.4);"
                          "}"
                          "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
                          "  width: 0px;"
                          "}")
        .arg(textHex,      // %1
             windowHex,    // %2
             windowAltHex, // %3
             borderHex,    // %4
             accentHex,    // %5
             selTextHex,   // %6
             viewHex,      // %7
             viewAltHex,   // %8
             subTextHex,   // %9
             navHover      // %10
        );
}

QString StyleHelper::darkThemeStyle(const QString& accentColor) {
    ThemeColors c = ThemeManager::instance().colors();
    if (!accentColor.isEmpty()) {
        c.accent = QColor(accentColor);
        c.selection = c.accent;
    }
    return darkThemeStyle(c);
}

QString StyleHelper::segmentedButtonLeftStyle(bool isDark, const QString& accentColor) {
    const auto& c = ThemeManager::instance().colors();
    QString acc = !accentColor.isEmpty() ? accentColor : c.accent.name();
    QString btnBg = c.button.name();
    QString btnFg = c.buttonText.name();
    QString border = c.border.name();
    QString hoverBg = isDark ? c.button.lighter(125).name() : c.button.darker(108).name();
    QString disabledBg = isDark ? c.window.name() : c.windowAlt.name();
    QString disabledFg = c.subText.name();

    return QStringLiteral(
               "QToolButton {"
               "  background: %1;"
               "  color: %2;"
               "  border: 1px solid %3;"
               "  border-top-left-radius: 6px;"
               "  border-bottom-left-radius: 6px;"
               "  border-top-right-radius: 0px;"
               "  border-bottom-right-radius: 0px;"
               "  padding: 4px 8px;"
               "  margin-right: -1px;"
               "}"
               "QToolButton:hover { background: %4; }"
               "QToolButton:checked { background: %5; color: #ffffff; border-color: %5; }"
               "QToolButton:disabled { color: %6; background: %7; border-color: %3; }")
        .arg(btnBg, btnFg, border, hoverBg, acc, disabledFg, disabledBg);
}

QString StyleHelper::segmentedButtonMidStyle(bool isDark, const QString& accentColor) {
    const auto& c = ThemeManager::instance().colors();
    QString acc = !accentColor.isEmpty() ? accentColor : c.accent.name();
    QString btnBg = c.button.name();
    QString btnFg = c.buttonText.name();
    QString border = c.border.name();
    QString hoverBg = isDark ? c.button.lighter(125).name() : c.button.darker(108).name();
    QString disabledBg = isDark ? c.window.name() : c.windowAlt.name();
    QString disabledFg = c.subText.name();

    return QStringLiteral(
               "QToolButton {"
               "  background: %1;"
               "  color: %2;"
               "  border: 1px solid %3;"
               "  border-radius: 0px;"
               "  padding: 4px 8px;"
               "  margin-right: -1px;"
               "}"
               "QToolButton:hover { background: %4; }"
               "QToolButton:checked { background: %5; color: #ffffff; border-color: %5; }"
               "QToolButton:disabled { color: %6; background: %7; border-color: %3; }")
        .arg(btnBg, btnFg, border, hoverBg, acc, disabledFg, disabledBg);
}

QString StyleHelper::segmentedButtonRightStyle(bool isDark, const QString& accentColor) {
    const auto& c = ThemeManager::instance().colors();
    QString acc = !accentColor.isEmpty() ? accentColor : c.accent.name();
    QString btnBg = c.button.name();
    QString btnFg = c.buttonText.name();
    QString border = c.border.name();
    QString hoverBg = isDark ? c.button.lighter(125).name() : c.button.darker(108).name();
    QString disabledBg = isDark ? c.window.name() : c.windowAlt.name();
    QString disabledFg = c.subText.name();

    return QStringLiteral(
               "QToolButton {"
               "  background: %1;"
               "  color: %2;"
               "  border: 1px solid %3;"
               "  border-top-right-radius: 6px;"
               "  border-bottom-right-radius: 6px;"
               "  border-top-left-radius: 0px;"
               "  border-bottom-left-radius: 0px;"
               "  padding: 4px 8px;"
               "}"
               "QToolButton:hover { background: %4; }"
               "QToolButton:checked { background: %5; color: #ffffff; border-color: %5; }"
               "QToolButton:disabled { color: %6; background: %7; border-color: %3; }")
        .arg(btnBg, btnFg, border, hoverBg, acc, disabledFg, disabledBg);
}

QString StyleHelper::segmentedButtonSingleStyle(bool isDark, const QString& accentColor) {
    const auto& c = ThemeManager::instance().colors();
    QString acc = !accentColor.isEmpty() ? accentColor : c.accent.name();
    QString btnBg = c.button.name();
    QString btnFg = c.buttonText.name();
    QString border = c.border.name();
    QString hoverBg = isDark ? c.button.lighter(125).name() : c.button.darker(108).name();
    QString disabledBg = isDark ? c.window.name() : c.windowAlt.name();
    QString disabledFg = c.subText.name();

    return QStringLiteral(
               "QToolButton {"
               "  background: %1;"
               "  color: %2;"
               "  border: 1px solid %3;"
               "  border-radius: 6px;"
               "  padding: 4px 10px;"
               "}"
               "QToolButton:hover { background: %4; }"
               "QToolButton:checked { background: %5; color: #ffffff; border-color: %5; }"
               "QToolButton:disabled { color: %6; background: %7; border-color: %3; }"
               "QToolButton::menu-indicator {"
               "  image: url(:/icons/chevron-down.svg);"
               "  subcontrol-origin: padding;"
               "  subcontrol-position: center right;"
               "  width: 8px;"
               "  height: 8px;"
               "  right: 4px;"
               "}")
        .arg(btnBg, btnFg, border, hoverBg, acc, disabledFg, disabledBg);
}

QString StyleHelper::searchBarStyle(bool isDark, const QString& accentColor) {
    const auto& c = ThemeManager::instance().colors();
    QString acc = !accentColor.isEmpty() ? accentColor : c.accent.name();
    QString bg = c.view.name();
    QString fg = c.text.name();
    QString border = c.border.name();

    return QStringLiteral("QLineEdit {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: 15px;"
                          "  padding: 4px 12px;"
                          "  font-size: 12px;"
                          "}"
                          "QLineEdit:focus {"
                          "  border: 1px solid %4;"
                          "}")
        .arg(c.button.name(), fg, border, acc);
}

QString StyleHelper::breadcrumbButtonStyle(bool isDark) {
    const auto& c = ThemeManager::instance().colors();
    QString fg = c.text.name();
    QString hoverBg = isDark ? c.button.lighter(125).name() : c.button.darker(108).name();

    return QStringLiteral("QPushButton {"
                          "  background: transparent;"
                          "  color: %1;"
                          "  border: none;"
                          "  border-radius: 4px;"
                          "  padding: 3px 7px;"
                          "  font-size: 12px;"
                          "}"
                          "QPushButton:hover {"
                          "  background: %2;"
                          "  color: %1;"
                          "}")
        .arg(fg, hoverBg);
}

QString StyleHelper::sidebarStyle(bool isDark, const QString& accentColor) {
    const auto& c = ThemeManager::instance().colors();
    QString acc = !accentColor.isEmpty() ? accentColor : c.accent.name();
    QString bg = c.window.name();
    QString border = c.border.name();
    QString fg = c.text.name();
    QString hoverBg = isDark ? QStringLiteral("rgba(255, 255, 255, 0.08)")
                             : QStringLiteral("rgba(0, 0, 0, 0.05)");
    QString selText = c.selectionText.name();

    return QStringLiteral("QTreeWidget#sidebar {"
                          "  background-color: %1;"
                          "  border: none;"
                          "  border-right: 1px solid %2;"
                          "  color: %3;"
                          "  font-size: 12px;"
                          "}"
                          "QTreeWidget#sidebar::item {"
                          "  color: %3;"
                          "  padding: 2px 4px;"
                          "  border-radius: 4px;"
                          "  margin: 1px 3px;"
                          "}"
                          "QTreeWidget#sidebar::item:hover {"
                          "  background-color: %4;"
                          "}"
                          "QTreeWidget#sidebar::item:selected {"
                          "  background-color: %5;"
                          "  color: %6;"
                          "  font-weight: 500;"
                          "}")
        .arg(bg, border, fg, hoverBg, acc, selText);
}
