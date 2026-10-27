#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "XdgTrash.h"

class TestXdgTrash : public QObject {
    Q_OBJECT

private slots:
    void testTrashAndRestore();
    void testEmptyTrash();
};

void TestXdgTrash::testTrashAndRestore()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    XdgTrash::setCustomTrashDirectory(tempDir.path() + QStringLiteral("/Trash"));

    QString testFile = tempDir.filePath(QStringLiteral("trash_target.txt"));
    QFile f(testFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("Trash me!");
    f.close();
    QVERIFY(QFile::exists(testFile));

    QString err;
    bool ok = XdgTrash::moveToTrash(testFile, &err);
    QVERIFY2(ok, qPrintable(err));
    QVERIFY(!QFile::exists(testFile));

    // Verify it appears in trash list
    QVector<TrashItem> items = XdgTrash::listTrashItems();
    bool found = false;
    QString trashedPath;
    for (const auto& item : items) {
        if (item.originalPath == testFile) {
            found = true;
            trashedPath = item.trashFilePath;
            break;
        }
    }
    QVERIFY(found);

    // Now restore it
    ok = XdgTrash::restoreItem(trashedPath, &err);
    QVERIFY2(ok, qPrintable(err));
    QVERIFY(QFile::exists(testFile));
}

void TestXdgTrash::testEmptyTrash()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    XdgTrash::setCustomTrashDirectory(tempDir.path() + QStringLiteral("/Trash"));

    QString testFile = tempDir.filePath(QStringLiteral("to_empty.txt"));
    QFile f(testFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("Empty me!");
    f.close();

    XdgTrash::moveToTrash(testFile);
    QVERIFY(!QFile::exists(testFile));

    QString err;
    bool ok = XdgTrash::emptyTrash(&err);
    QVERIFY2(ok, qPrintable(err));
}

QTEST_MAIN(TestXdgTrash)
#include "test_xdgtrash.moc"
