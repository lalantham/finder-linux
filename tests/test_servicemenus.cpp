#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include "ServiceMenuManager.h"

class TestServiceMenus : public QObject {
    Q_OBJECT

private slots:
    void testDirectoryActionHonorsMaxUrls();
    void testServiceTypesWithoutMimeType();
    void testExecFieldExpansion();
    void testInstalledKdeServiceMenus();

private:
    static void writeFile(const QString& path, const QByteArray& contents);
    static QString makeMenuDirectory(const QTemporaryDir& directory);
};

void TestServiceMenus::writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(contents);
    file.close();
}

QString TestServiceMenus::makeMenuDirectory(const QTemporaryDir& directory)
{
    const QString menuDirectory = directory.path() + QStringLiteral("/kio/servicemenus");
    QDir().mkpath(menuDirectory);
    return menuDirectory;
}

void TestServiceMenus::testDirectoryActionHonorsMaxUrls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString menuDirectory = makeMenuDirectory(directory);

    writeFile(menuDirectory + QStringLiteral("/filelight.desktop"), R"(
[Desktop Entry]
Type=Service
MimeType=inode/directory;
Actions=filelight
X-KDE-MaxNumberOfUrls=1

[Desktop Action filelight]
Name=View Disk Usage Statistics
Icon=filelight
Exec=filelight %u
)");

    const QString firstFolder = directory.path() + QStringLiteral("/folder1");
    const QString secondFolder = directory.path() + QStringLiteral("/folder2");
    QVERIFY(QDir().mkpath(firstFolder));
    QVERIFY(QDir().mkpath(secondFolder));

    const QVector<ServiceMenuAction> single =
        ServiceMenuManager::actionsFor({firstFolder}, {menuDirectory});
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.first().name, QStringLiteral("View Disk Usage Statistics"));
    QCOMPARE(single.first().actionName, QStringLiteral("filelight"));

    QVERIFY(ServiceMenuManager::actionsFor({firstFolder, secondFolder}, {menuDirectory}).isEmpty());
}

void TestServiceMenus::testServiceTypesWithoutMimeType()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString menuDirectory = makeMenuDirectory(directory);

    writeFile(menuDirectory + QStringLiteral("/installfont.desktop"), R"(
[Desktop Entry]
X-KDE-ServiceTypes=KonqPopupMenu/Plugin,text/plain
Actions=installFont;
Type=Service

[Desktop Action installFont]
Name=Install...
Exec=echo %U
)");

    const QString filePath = directory.path() + QStringLiteral("/note.txt");
    writeFile(filePath, "hello");

    const QVector<ServiceMenuAction> actions =
        ServiceMenuManager::actionsFor({filePath}, {menuDirectory});
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.first().name, QStringLiteral("Install..."));
}

void TestServiceMenus::testExecFieldExpansion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString menuDirectory = makeMenuDirectory(directory);

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

    writeFile(menuDirectory + QStringLiteral("/collect.desktop"),
              QStringLiteral(R"(
[Desktop Entry]
Type=Service
MimeType=text/plain;
Actions=collect

[Desktop Action collect]
Name=Collect
Exec=%1 %F
)").arg(scriptPath).toUtf8());

    const QString firstFile = directory.path() + QStringLiteral("/a.txt");
    const QString secondFile = directory.path() + QStringLiteral("/b.txt");
    writeFile(firstFile, "a");
    writeFile(secondFile, "b");

    const QVector<ServiceMenuAction> actions =
        ServiceMenuManager::actionsFor({firstFile, secondFile}, {menuDirectory});
    QCOMPARE(actions.size(), 1);
    QVERIFY(ServiceMenuManager::launch(actions.first(), {firstFile, secondFile}));

    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(markerPath), 3000);
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList arguments =
        QString::fromUtf8(marker.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(arguments, (QStringList{firstFile, secondFile}));
}


void TestServiceMenus::testInstalledKdeServiceMenus()
{
    if (!QFile::exists(QStringLiteral("/usr/share/kio/servicemenus/filelight.desktop"))) {
        QSKIP("KDE filelight service menu is not installed");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString targetFolder = directory.path() + QStringLiteral("/target");
    QVERIFY(QDir().mkpath(targetFolder));

    const QVector<ServiceMenuAction> actions =
        ServiceMenuManager::actionsFor({targetFolder}, ServiceMenuManager::searchPaths());
    bool found = false;
    for (const auto& action : actions) {
        if (action.actionName == QStringLiteral("filelight")) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "filelight service menu did not match a directory selection");
}

QTEST_MAIN(TestServiceMenus)
#include "test_servicemenus.moc"
