#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "UndoManager.h"

class TestUndo : public QObject {
    Q_OBJECT

private slots:
    void testUndoRename();
    void testUndoMove();
    void testRedoMove();
    void testUndoSymlinkMove();
    void testUndoCreateRemovesEmptyFolder();
    void testUndoCreateRefusesNonEmptyFolder();
    void testUndoCreateRefusesModifiedFile();
    void testRedoCreateDoesNotTruncateExistingFile();
    void testRedoCreateRecreatesItem();
};

void TestUndo::testUndoRename()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString original = tempDir.filePath(QStringLiteral("original.txt"));
    QString renamed = tempDir.filePath(QStringLiteral("renamed.txt"));

    QFile f(original);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("content");
    f.close();

    QVERIFY(QFile::rename(original, renamed));

    UndoManager undoMgr;
    UndoAction action;
    action.type = UndoActionType::Rename;
    action.description = QStringLiteral("Rename file");
    action.sourcePath = original;
    action.targetPath = renamed;

    undoMgr.pushAction(action);
    QVERIFY(undoMgr.canUndo());

    QString err;
    bool ok = undoMgr.undo(&err);
    QVERIFY2(ok, qPrintable(err));

    // Verify it was renamed back
    QVERIFY(QFile::exists(original));
    QVERIFY(!QFile::exists(renamed));
}

void TestUndo::testUndoMove()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString sub1 = tempDir.filePath(QStringLiteral("dir1"));
    QString sub2 = tempDir.filePath(QStringLiteral("dir2"));
    QDir().mkpath(sub1);
    QDir().mkpath(sub2);

    QString original = sub1 + QStringLiteral("/item.txt");
    QString moved = sub2 + QStringLiteral("/item.txt");

    QFile f(original);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello move");
    f.close();

    QVERIFY(QFile::rename(original, moved));

    UndoManager undoMgr;
    UndoAction action;
    action.type = UndoActionType::Move;
    action.description = QStringLiteral("Move item.txt");
    action.sourcePath = original;
    action.targetPath = moved;

    undoMgr.pushAction(action);
    QVERIFY(undoMgr.canUndo());

    QString err;
    bool ok = undoMgr.undo(&err);
    QVERIFY2(ok, qPrintable(err));
    QVERIFY(QFile::exists(original));
    QVERIFY(!QFile::exists(moved));
}

void TestUndo::testRedoMove()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString original = tempDir.filePath(QStringLiteral("file1.txt"));
    QString moved = tempDir.filePath(QStringLiteral("file2.txt"));

    QFile f(original);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("data");
    f.close();

    QVERIFY(QFile::rename(original, moved));

    UndoManager undoMgr;
    UndoAction action;
    action.type = UndoActionType::Move;
    action.sourcePath = original;
    action.targetPath = moved;
    undoMgr.pushAction(action);

    QString err;
    QVERIFY(undoMgr.undo(&err));
    QVERIFY(QFile::exists(original));
    QVERIFY(!QFile::exists(moved));
    QVERIFY(undoMgr.canRedo());

    QVERIFY(undoMgr.redo(&err));
    QVERIFY(!QFile::exists(original));
    QVERIFY(QFile::exists(moved));
}

void TestUndo::testUndoSymlinkMove()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString target = tempDir.filePath(QStringLiteral("target.txt"));
    QFile f(target);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("link content");
    f.close();

    QString symlinkOrig = tempDir.filePath(QStringLiteral("link1"));
    QString symlinkMoved = tempDir.filePath(QStringLiteral("link2"));
    QVERIFY(QFile::link(target, symlinkOrig));
    QVERIFY(QFile::rename(symlinkOrig, symlinkMoved));

    UndoManager undoMgr;
    UndoAction action;
    action.type = UndoActionType::Move;
    action.sourcePath = symlinkOrig;
    action.targetPath = symlinkMoved;
    undoMgr.pushAction(action);

    QString err;
    QVERIFY(undoMgr.undo(&err));
    QVERIFY(QFileInfo(symlinkOrig).isSymLink());
    QVERIFY(!QFileInfo::exists(symlinkMoved) && !QFileInfo(symlinkMoved).isSymLink());
}

void TestUndo::testUndoCreateRemovesEmptyFolder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folder = tempDir.filePath(QStringLiteral("created"));
    QVERIFY(QDir().mkpath(folder));

    UndoManager undoMgr;
    UndoAction action;
    action.type = UndoActionType::Create;
    action.isDirectory = true;
    action.targetPath = folder;
    action.createdSize = 0;
    action.createdModified = QFileInfo(folder).lastModified();
    undoMgr.pushAction(action);

    QString err;
    QVERIFY2(undoMgr.undo(&err), qPrintable(err));
    QVERIFY(!QDir(folder).exists());
}

void TestUndo::testUndoCreateRefusesNonEmptyFolder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString folder = tempDir.filePath(QStringLiteral("created"));
    QVERIFY(QDir().mkpath(folder));
    const QString inside = folder + QStringLiteral("/important.txt");
    QFile f(inside);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("do not lose me");
    f.close();

    UndoManager undoMgr;
    UndoAction action;
    action.type = UndoActionType::Create;
    action.isDirectory = true;
    action.targetPath = folder;
    action.createdSize = 0;
    action.createdModified = QFileInfo(folder).lastModified();
    undoMgr.pushAction(action);

    QString err;
    QVERIFY(!undoMgr.undo(&err));
    QVERIFY(!err.isEmpty());
    QVERIFY(QFile::exists(inside));
    QVERIFY(QFile(inside).open(QIODevice::ReadOnly));
}

void TestUndo::testUndoCreateRefusesModifiedFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString file = tempDir.filePath(QStringLiteral("created.txt"));
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("initial");
    f.close();

    UndoAction action;
    action.type = UndoActionType::Create;
    action.targetPath = file;
    action.isDirectory = false;
    QFileInfo created(file);
    action.createdSize = created.size();
    action.createdModified = created.lastModified();

    // The user edits the file after creating it.
    QFile edit(file);
    QVERIFY(edit.open(QIODevice::WriteOnly | QIODevice::Truncate));
    edit.write("edited and much longer");
    edit.close();

    UndoManager undoMgr;
    undoMgr.pushAction(action);

    QString err;
    QVERIFY(!undoMgr.undo(&err));
    QVERIFY(!err.isEmpty());
    QVERIFY(QFile::exists(file));
}

void TestUndo::testRedoCreateDoesNotTruncateExistingFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString file = tempDir.filePath(QStringLiteral("new.txt"));
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    UndoAction action;
    action.type = UndoActionType::Create;
    action.targetPath = file;
    action.isDirectory = false;
    QFileInfo created(file);
    action.createdSize = created.size();
    action.createdModified = created.lastModified();

    UndoManager undoMgr;
    undoMgr.pushAction(action);

    QString err;
    QVERIFY2(undoMgr.undo(&err), qPrintable(err));
    QVERIFY(!QFile::exists(file));

    // A different file appears at the same path before redo.
    QFile precious(file);
    QVERIFY(precious.open(QIODevice::WriteOnly));
    precious.write("precious data");
    precious.close();

    QVERIFY(!undoMgr.redo(&err));
    QFile check(file);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("precious data"));
}

void TestUndo::testRedoCreateRecreatesItem()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString file = tempDir.filePath(QStringLiteral("new.txt"));
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    UndoAction action;
    action.type = UndoActionType::Create;
    action.targetPath = file;
    action.isDirectory = false;
    action.createdSize = 0;
    action.createdModified = QFileInfo(file).lastModified();

    UndoManager undoMgr;
    undoMgr.pushAction(action);

    QString err;
    QVERIFY2(undoMgr.undo(&err), qPrintable(err));
    QVERIFY(!QFile::exists(file));
    QVERIFY2(undoMgr.redo(&err), qPrintable(err));
    QVERIFY(QFile::exists(file));
}

QTEST_MAIN(TestUndo)
#include "test_undo.moc"
