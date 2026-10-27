#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include "FileOperationEngine.h"

class TestOperations : public QObject {
    Q_OBJECT

private slots:
    void testCreateFolderAndFile();
    void testRenameItem();
    void testCopyFile();
    void testMoveFile();
};

void TestOperations::testCreateFolderAndFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FileOperationEngine engine;
    QString createdFolder, error;
    bool ok = engine.createFolder(tempDir.path(), QStringLiteral("MyFolder"), &createdFolder, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QDir(createdFolder).exists());

    QString createdFile;
    ok = engine.createFile(createdFolder, QStringLiteral("notes.txt"), &createdFile, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFile::exists(createdFile));
}

void TestOperations::testRenameItem()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FileOperationEngine engine;
    QString filePath = tempDir.filePath(QStringLiteral("original.txt"));
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("Content");
    f.close();

    QString renamedPath, error;
    bool ok = engine.renameItem(filePath, QStringLiteral("renamed.txt"), &renamedPath, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!QFile::exists(filePath));
    QVERIFY(QFile::exists(renamedPath));
}

void TestOperations::testCopyFile()
{
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    QString srcFile = srcDir.filePath(QStringLiteral("source.txt"));
    QFile f(srcFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("Payload to copy");
    f.close();

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);

    engine.copyItems(QStringList{srcFile}, dstDir.path(), ConflictResolution::Replace);
    QVERIFY(spy.wait(5000));

    QString expectedDst = dstDir.filePath(QStringLiteral("source.txt"));
    QVERIFY(QFile::exists(expectedDst));
    QVERIFY(QFile::exists(srcFile)); // Source intact
}

void TestOperations::testMoveFile()
{
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    QString srcFile = srcDir.filePath(QStringLiteral("to_move.txt"));
    QFile f(srcFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("Payload to move");
    f.close();

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);

    engine.moveItems(QStringList{srcFile}, dstDir.path(), ConflictResolution::Replace);
    QVERIFY(spy.wait(5000));

    QString expectedDst = dstDir.filePath(QStringLiteral("to_move.txt"));
    QVERIFY(QFile::exists(expectedDst));
    QVERIFY(!QFile::exists(srcFile)); // Source removed
}

QTEST_MAIN(TestOperations)
#include "test_operations.moc"
