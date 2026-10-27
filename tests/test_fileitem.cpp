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
    f.write("Hello macOS Finder on Linux!");
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
}

QTEST_MAIN(TestFileItem)
#include "test_fileitem.moc"
