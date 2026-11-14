#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct ServiceMenuAction {
    QString name;
    QString comment;
    QString icon;
    QString exec;
    QString submenu;
    QString desktopFile;
    QString actionName;
    int priority{0};
};

// Reads KDE-style service menus (Dolphin/Konqueror "Actions") from the
// standard kio/servicemenus and kservices5/ServiceMenus locations.
class ServiceMenuManager {
public:
    // Returns actions that apply to the current selection. When
    // searchDirectories is empty the standard locations are used.
    static QVector<ServiceMenuAction> actionsFor(
        const QStringList& paths,
        const QStringList& searchDirectories = QStringList());

    static bool launch(const ServiceMenuAction& action, const QStringList& paths);

    static QStringList searchPaths();
};
