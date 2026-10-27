#include "MainWindow.h"
#include "AppSettings.h"
#include "ThemeManager.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Finder"));
    app.setApplicationDisplayName(QStringLiteral("Finder"));
    app.setOrganizationName(QStringLiteral("FinderLinux"));
    app.setOrganizationDomain(QStringLiteral("finder.linux"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    // Apply configured theme
    ThemeManager::instance().applyTheme(AppSettings::instance().theme());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Finder for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("directory"), QStringLiteral("Directory to open"));

    parser.process(app);

    MainWindow window;

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty() && QDir(args.first()).exists()) {
        window.openPath(args.first());
    }

    window.show();
    return app.exec();
}
