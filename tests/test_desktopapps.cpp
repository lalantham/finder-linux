#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include "DesktopAppsManager.h"
#include "RecentFilesManager.h"

class TestDesktopApps : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();
    void testInheritanceAndVisibility();
    void testOnlyShowInMatchesCurrentDesktop();
    void testExplicitAssociationCanShowNoDisplayApp();
    void testMissingDesktopFileIsIgnored();
    void testAppImageExecutesDirectly();
    void testCustomCommandMultiFile();

private:
    static void writeFile(const QString& path, const QByteArray& contents);
    static QSet<QString> idsFor(const QString& mimeType);
    static bool contains(const QSet<QString>& ids, const QString& id);

    QTemporaryDir m_dataDir;
    QTemporaryDir m_configDir;
};

void TestDesktopApps::writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(contents);
    file.close();
}

QSet<QString> TestDesktopApps::idsFor(const QString& mimeType)
{
    QSet<QString> ids;
    for (const auto& app : DesktopAppsManager::appsForMime(mimeType)) {
        ids.insert(app.id);
    }
    return ids;
}

bool TestDesktopApps::contains(const QSet<QString>& ids, const QString& id)
{
    return ids.contains(id);
}

void TestDesktopApps::initTestCase()
{
    QVERIFY(m_dataDir.isValid());
    QVERIFY(m_configDir.isValid());
    RecentFilesManager::setStoragePathForTesting(
        m_dataDir.path() + QStringLiteral("/recently-used.xbel"));

    const QString appsDir = m_dataDir.path() + QStringLiteral("/applications");
    QVERIFY(QDir().mkpath(appsDir));

    writeFile(appsDir + QStringLiteral("/viewer.desktop"),
              "[Desktop Entry]\nType=Application\nName=Viewer\nExec=viewer %f\nMimeType=text/plain;\n");
    writeFile(appsDir + QStringLiteral("/exact.desktop"),
              "[Desktop Entry]\nType=Application\nName=Exact\nExec=exact %f\nMimeType=text/x-python;\n");
    writeFile(appsDir + QStringLiteral("/added.desktop"),
              "[Desktop Entry]\nType=Application\nName=Added\nExec=added %f\n");
    writeFile(appsDir + QStringLiteral("/explicitnode.desktop"),
              "[Desktop Entry]\nType=Application\nName=Explicit\nExec=explicit %f\nNoDisplay=true\nMimeType=text/plain;\n");
    writeFile(appsDir + QStringLiteral("/removed.desktop"),
              "[Desktop Entry]\nType=Application\nName=Removed\nExec=removed %f\nMimeType=text/plain;\n");
    writeFile(appsDir + QStringLiteral("/hidden.desktop"),
              "[Desktop Entry]\nType=Application\nName=Hidden\nExec=hidden %f\nNoDisplay=true\nMimeType=text/plain;\n");
    writeFile(appsDir + QStringLiteral("/deleted.desktop"),
              "[Desktop Entry]\nType=Application\nName=Deleted\nExec=deleted %f\nHidden=true\nMimeType=text/plain;\n");
    writeFile(appsDir + QStringLiteral("/broken.desktop"),
              "[Desktop Entry]\nType=Application\nName=Broken\nExec=broken %f\nTryExec=definitely-not-installed-xyz\nMimeType=text/plain;\n");
    writeFile(appsDir + QStringLiteral("/onlykde.desktop"),
              "[Desktop Entry]\nType=Application\nName=OnlyKde\nExec=onlykde %f\nOnlyShowIn=KDE;\nMimeType=text/plain;\n");

    writeFile(appsDir + QStringLiteral("/mimeinfo.cache"),
              "[MIME Cache]\n"
              "text/x-python=exact.desktop;\n"
              "text/plain=viewer.desktop;removed.desktop;hidden.desktop;deleted.desktop;broken.desktop;onlykde.desktop;explicitnode.desktop;missing.desktop;\n");

    writeFile(m_configDir.path() + QStringLiteral("/mimeapps.list"),
              "[Default Applications]\n"
              "text/x-python=exact.desktop;\n"
              "\n"
              "[Added Associations]\n"
              "text/x-python=added.desktop;explicitnode.desktop;\n"
              "\n"
              "[Removed Associations]\n"
              "text/plain=removed.desktop;\n");

    qputenv("XDG_DATA_HOME", m_dataDir.path().toUtf8());
    // Keep the real shared-mime-info database available (it lives in
    // /usr/share/mime); only the applications dir is overridden.
    qputenv("XDG_DATA_DIRS",
            (m_dataDir.path() + QStringLiteral(":/usr/local/share:/usr/share")).toUtf8());
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", m_configDir.path().toUtf8());
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");
}

void TestDesktopApps::testInheritanceAndVisibility()
{
    const QSet<QString> ids = idsFor(QStringLiteral("text/x-python"));

    // Exact handler, inherited text/plain handler, and mimeapps addition.
    QVERIFY2(contains(ids, QStringLiteral("exact.desktop")), "exact MIME handler missing");
    QVERIFY2(contains(ids, QStringLiteral("viewer.desktop")), "parent MIME handler missing");
    QVERIFY2(contains(ids, QStringLiteral("added.desktop")), "mimeapps added association missing");

    // Removed / hidden / deleted / broken / desktop-specific entries are filtered.
    QVERIFY(!contains(ids, QStringLiteral("removed.desktop")));
    QVERIFY(!contains(ids, QStringLiteral("hidden.desktop")));
    QVERIFY(!contains(ids, QStringLiteral("deleted.desktop")));
    QVERIFY(!contains(ids, QStringLiteral("broken.desktop")));
    QVERIFY(!contains(ids, QStringLiteral("onlykde.desktop")));
}

void TestDesktopApps::testOnlyShowInMatchesCurrentDesktop()
{
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    const QSet<QString> ids = idsFor(QStringLiteral("text/x-python"));
    QVERIFY2(contains(ids, QStringLiteral("onlykde.desktop")),
             "OnlyShowIn=KDE entry should be listed on KDE");
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");
}

void TestDesktopApps::testExplicitAssociationCanShowNoDisplayApp()
{
    const QSet<QString> ids = idsFor(QStringLiteral("text/x-python"));
    QVERIFY2(contains(ids, QStringLiteral("explicitnode.desktop")),
             "an app explicitly added in mimeapps.list should be listed even with NoDisplay");
}

void TestDesktopApps::testMissingDesktopFileIsIgnored()
{
    const QSet<QString> ids = idsFor(QStringLiteral("text/x-python"));
    QVERIFY(!contains(ids, QStringLiteral("missing.desktop")));
}

void TestDesktopApps::testAppImageExecutesDirectly()
{
    QTemporaryDir appImageDir;
    QVERIFY(appImageDir.isValid());

    const QString appImagePath = appImageDir.filePath(QStringLiteral("sample.AppImage"));
    const QString markerPath = appImageDir.filePath(QStringLiteral("launched.txt"));

    QFile script(appImagePath);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write(QStringLiteral("#!/bin/sh\nprintf launched > \"%1\"\n")
                     .arg(markerPath)
                     .toUtf8());
    script.close();

    QVERIFY(QFile::setPermissions(appImagePath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    if (!QFileInfo(appImagePath).isExecutable()) {
        QSKIP("Temporary filesystem is mounted noexec");
    }

    QVERIFY(DesktopAppsManager::launchFile(appImagePath));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 3000);
}

void TestDesktopApps::init()
{
    DesktopAppsManager::clearCaches();
}

void TestDesktopApps::cleanupTestCase()
{
    RecentFilesManager::setStoragePathForTesting(QString());
}

void TestDesktopApps::testCustomCommandMultiFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString markerPath = directory.path() + QStringLiteral("/args.txt");
    const QString scriptPath = directory.path() + QStringLiteral("/collect.sh");
    writeFile(scriptPath,
              QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"%1\"\n")
                  .arg(markerPath)
                  .toUtf8());
    QVERIFY(QFile::setPermissions(scriptPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    if (!QFileInfo(scriptPath).isExecutable()) {
        QSKIP("Temporary filesystem is mounted noexec");
    }

    const QString firstFile = directory.path() + QStringLiteral("/one.txt");
    const QString secondFile = directory.path() + QStringLiteral("/two.txt");
    writeFile(firstFile, "one");
    writeFile(secondFile, "two");

    QVERIFY(DesktopAppsManager::launchCustomCommand(
        QStringLiteral("%1 %F").arg(scriptPath), {firstFile, secondFile}));

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 3000);
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments =
        QString::fromUtf8(marker.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(arguments, (QStringList{firstFile, secondFile}));
}

QTEST_MAIN(TestDesktopApps)
#include "test_desktopapps.moc"
