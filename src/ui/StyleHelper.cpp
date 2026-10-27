#include "StyleHelper.h"

QString StyleHelper::lightThemeStyle()
{
    return QStringLiteral(
        "QMainWindow {"
        "  background-color: #f6f6f6;"
        "}"
        "QToolBar {"
        "  background-color: #ececec;"
        "  border-bottom: 1px solid #d1d1d1;"
        "  spacing: 6px;"
        "  padding: 6px 10px;"
        "}"
        "QSplitter::handle {"
        "  background-color: #d1d1d1;"
        "  width: 1px;"
        "}"
        "QTreeView, QListView, QTableView {"
        "  background-color: #ffffff;"
        "  alternate-background-color: #fbfbfb;"
        "  border: none;"
        "  selection-background-color: #007aff;"
        "  selection-color: #ffffff;"
        "  outline: none;"
        "  font-size: 13px;"
        "}"
        "QHeaderView::section {"
        "  background-color: #f6f6f6;"
        "  color: #6e6e73;"
        "  padding: 4px 8px;"
        "  border: none;"
        "  border-right: 1px solid #e5e5e5;"
        "  border-bottom: 1px solid #dcdcdc;"
        "  font-weight: 500;"
        "  font-size: 11px;"
        "}"
        "QStatusBar {"
        "  background-color: #ececec;"
        "  border-top: 1px solid #dcdcdc;"
        "  color: #6e6e73;"
        "  font-size: 11px;"
        "}"
        "QTabBar::tab {"
        "  background: #e0e0e0;"
        "  color: #444444;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  padding: 6px 14px;"
        "  margin-right: 2px;"
        "  font-size: 12px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #ffffff;"
        "  color: #000000;"
        "  font-weight: bold;"
        "}"
        "QMenu {"
        "  background-color: #fdfdfd;"
        "  border: 1px solid #d1d1d1;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  padding: 6px 20px;"
        "  border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #007aff;"
        "  color: #ffffff;"
        "}"
    );
}

QString StyleHelper::darkThemeStyle()
{
    return QStringLiteral(
        "QMainWindow {"
        "  background-color: #1e1e1e;"
        "}"
        "QToolBar {"
        "  background-color: #2b2b2c;"
        "  border-bottom: 1px solid #3c3d40;"
        "  spacing: 6px;"
        "  padding: 6px 10px;"
        "}"
        "QSplitter::handle {"
        "  background-color: #3c3d40;"
        "  width: 1px;"
        "}"
        "QTreeView, QListView, QTableView {"
        "  background-color: #1e1e1e;"
        "  alternate-background-color: #252526;"
        "  border: none;"
        "  color: #e0e0e0;"
        "  selection-background-color: #0a84ff;"
        "  selection-color: #ffffff;"
        "  outline: none;"
        "  font-size: 13px;"
        "}"
        "QHeaderView::section {"
        "  background-color: #2b2b2c;"
        "  color: #98989d;"
        "  padding: 4px 8px;"
        "  border: none;"
        "  border-right: 1px solid #38383a;"
        "  border-bottom: 1px solid #38383a;"
        "  font-weight: 500;"
        "  font-size: 11px;"
        "}"
        "QStatusBar {"
        "  background-color: #2b2b2c;"
        "  border-top: 1px solid #38383a;"
        "  color: #98989d;"
        "  font-size: 11px;"
        "}"
        "QTabBar::tab {"
        "  background: #252526;"
        "  color: #a0a0a0;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  padding: 6px 14px;"
        "  margin-right: 2px;"
        "  font-size: 12px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #1e1e1e;"
        "  color: #ffffff;"
        "  font-weight: bold;"
        "}"
        "QMenu {"
        "  background-color: #2d2d30;"
        "  color: #e0e0e0;"
        "  border: 1px solid #3c3d40;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  padding: 6px 20px;"
        "  border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #0a84ff;"
        "  color: #ffffff;"
        "}"
    );
}

QString StyleHelper::segmentedButtonLeftStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QToolButton {"
            "  background: #38383a;"
            "  color: #e0e0e0;"
            "  border: 1px solid #4a4a4c;"
            "  border-top-left-radius: 6px;"
            "  border-bottom-left-radius: 6px;"
            "  border-top-right-radius: 0px;"
            "  border-bottom-right-radius: 0px;"
            "  padding: 4px 8px;"
            "  margin-right: -1px;"
            "}"
            "QToolButton:hover { background: #454548; }"
            "QToolButton:checked { background: #0a84ff; color: #ffffff; border-color: #0a84ff; }"
        );
    }
    return QStringLiteral(
        "QToolButton {"
        "  background: #fdfdfd;"
        "  color: #333333;"
        "  border: 1px solid #c8c8c8;"
        "  border-top-left-radius: 6px;"
        "  border-bottom-left-radius: 6px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  padding: 4px 8px;"
        "  margin-right: -1px;"
        "}"
        "QToolButton:hover { background: #ebebeb; }"
        "QToolButton:checked { background: #007aff; color: #ffffff; border-color: #007aff; }"
    );
}

QString StyleHelper::segmentedButtonMidStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QToolButton {"
            "  background: #38383a;"
            "  color: #e0e0e0;"
            "  border: 1px solid #4a4a4c;"
            "  border-radius: 0px;"
            "  padding: 4px 8px;"
            "  margin-right: -1px;"
            "}"
            "QToolButton:hover { background: #454548; }"
            "QToolButton:checked { background: #0a84ff; color: #ffffff; border-color: #0a84ff; }"
        );
    }
    return QStringLiteral(
        "QToolButton {"
        "  background: #fdfdfd;"
        "  color: #333333;"
        "  border: 1px solid #c8c8c8;"
        "  border-radius: 0px;"
        "  padding: 4px 8px;"
        "  margin-right: -1px;"
        "}"
        "QToolButton:hover { background: #ebebeb; }"
        "QToolButton:checked { background: #007aff; color: #ffffff; border-color: #007aff; }"
    );
}

QString StyleHelper::segmentedButtonRightStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QToolButton {"
            "  background: #38383a;"
            "  color: #e0e0e0;"
            "  border: 1px solid #4a4a4c;"
            "  border-top-right-radius: 6px;"
            "  border-bottom-right-radius: 6px;"
            "  border-top-left-radius: 0px;"
            "  border-bottom-left-radius: 0px;"
            "  padding: 4px 8px;"
            "}"
            "QToolButton:hover { background: #454548; }"
            "QToolButton:checked { background: #0a84ff; color: #ffffff; border-color: #0a84ff; }"
        );
    }
    return QStringLiteral(
        "QToolButton {"
        "  background: #fdfdfd;"
        "  color: #333333;"
        "  border: 1px solid #c8c8c8;"
        "  border-top-right-radius: 6px;"
        "  border-bottom-right-radius: 6px;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  padding: 4px 8px;"
        "}"
        "QToolButton:hover { background: #ebebeb; }"
        "QToolButton:checked { background: #007aff; color: #ffffff; border-color: #007aff; }"
    );
}

QString StyleHelper::segmentedButtonSingleStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QToolButton {"
            "  background: #38383a;"
            "  color: #e0e0e0;"
            "  border: 1px solid #4a4a4c;"
            "  border-radius: 6px;"
            "  padding: 4px 10px;"
            "}"
            "QToolButton:hover { background: #454548; }"
            "QToolButton:checked { background: #0a84ff; color: #ffffff; border-color: #0a84ff; }"
        );
    }
    return QStringLiteral(
        "QToolButton {"
        "  background: #fdfdfd;"
        "  color: #333333;"
        "  border: 1px solid #c8c8c8;"
        "  border-radius: 6px;"
        "  padding: 4px 10px;"
        "}"
        "QToolButton:hover { background: #ebebeb; }"
        "QToolButton:checked { background: #007aff; color: #ffffff; border-color: #007aff; }"
    );
}

QString StyleHelper::searchBarStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QLineEdit {"
            "  background-color: #38383a;"
            "  color: #ffffff;"
            "  border: 1px solid #4a4a4c;"
            "  border-radius: 13px;"
            "  padding: 3px 12px;"
            "  font-size: 12px;"
            "}"
            "QLineEdit:focus {"
            "  border: 1px solid #0a84ff;"
            "}"
        );
    }
    return QStringLiteral(
        "QLineEdit {"
        "  background-color: #ffffff;"
        "  color: #1d1d1f;"
        "  border: 1px solid #c8c8c8;"
        "  border-radius: 13px;"
        "  padding: 3px 12px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #007aff;"
        "}"
    );
}

QString StyleHelper::breadcrumbButtonStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QPushButton {"
            "  background: transparent;"
            "  color: #cccccc;"
            "  border: none;"
            "  border-radius: 4px;"
            "  padding: 2px 6px;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover {"
            "  background: #38383a;"
            "  color: #ffffff;"
            "}"
        );
    }
    return QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  color: #444444;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background: #e0e0e0;"
        "  color: #000000;"
        "}"
    );
}

QString StyleHelper::sidebarStyle(bool isDark)
{
    if (isDark) {
        return QStringLiteral(
            "QTreeWidget {"
            "  background-color: #242528;"
            "  border: none;"
            "  color: #cccccc;"
            "  font-size: 13px;"
            "}"
            "QTreeWidget::item {"
            "  padding: 4px 6px;"
            "  border-radius: 6px;"
            "  margin: 1px 4px;"
            "}"
            "QTreeWidget::item:hover {"
            "  background-color: rgba(255, 255, 255, 0.06);"
            "}"
            "QTreeWidget::item:selected {"
            "  background-color: #0a84ff;"
            "  color: #ffffff;"
            "}"
        );
    }
    return QStringLiteral(
        "QTreeWidget {"
        "  background-color: #e9e9ea;"
        "  border: none;"
        "  color: #333333;"
        "  font-size: 13px;"
        "}"
        "QTreeWidget::item {"
        "  padding: 4px 6px;"
        "  border-radius: 6px;"
        "  margin: 1px 4px;"
        "}"
        "QTreeWidget::item:hover {"
        "  background-color: rgba(0, 0, 0, 0.05);"
        "}"
        "QTreeWidget::item:selected {"
        "  background-color: #007aff;"
        "  color: #ffffff;"
        "}"
    );
}
