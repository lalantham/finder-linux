#include <QTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include "FileItem.h"

class TestFileItem : public QObject {
    Q_OBJECT

private slots:
    void testFormatSize();
    void testFormatPermissions();
    void testItemProperties();
    void testCalculatedFolderSize();
};

void TestFileItem::testFormatSize()
{
    QCOMPARE(FileItem::formatSize(500), QStringLiteral("500 B"));
    QCOMPARE(FileItem::formatSize(1000), QStringLiteral("1.0 KB"));
    QCOMPARE(FileItem::formatSize(1500000), QStringLiteral("1.5 MB"));
    QCOMPARE(FileItem::formatSize(2500000000LL), QStringLiteral("2.5 GB"));
    QCOMPARE(FileItem::formatSize(-1), QStringLiteral("--"));
}

void TestFileItem::testFormatPermissions()
{
    mode_t regularMode = S_IFREG | 0644;
    QString regularPerms = FileItem::formatPermissions(regularMode);
    QCOMPARE(regularPerms, QStringLiteral("-rw-r--r--"));

    mode_t dirMode = S_IFDIR | 0755;
    QString dirPerms = FileItem::formatPermissions(dirMode);
    QCOMPARE(dirPerms, QStringLiteral("drwxr-xr-x"));
}

void TestFileItem::testItemProperties()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString filePath = tempDir.filePath(QStringLiteral("sample.txt"));
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("Hello file manager on Linux!");
    f.close();

    FileItem item(filePath);
    QVERIFY(item.exists());
    QVERIFY(item.isFile());
    QVERIFY(!item.isDir());
    QCOMPARE(item.fileName(), QStringLiteral("sample.txt"));
    QCOMPARE(item.baseName(), QStringLiteral("sample"));
    QCOMPARE(item.suffix(), QStringLiteral("txt"));
    QVERIFY(item.size() > 0);
    QVERIFY(item.isReadable());
    QVERIFY(item.inode() > 0);
    QVERIFY(item.deviceId() > 0);
    QVERIFY(item.hardLinks() >= 1);
}

void TestFileItem::testCalculatedFolderSize()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FileItem dirItem(tempDir.path());
    QVERIFY(dirItem.isDir());
    QCOMPARE(dirItem.size(), 0);
    QCOMPARE(dirItem.formattedSize(), QStringLiteral("--"));

    dirItem.setCalculatedFolderSize(2048576, 42);
    QCOMPARE(dirItem.size(), 2048576);
    QVERIFY(dirItem.formattedSize().contains(QStringLiteral("42 items")));
}

QTEST_MAIN(TestFileItem)
#include "test_fileitem.moc"
