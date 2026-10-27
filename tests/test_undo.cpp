#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "UndoManager.h"

class TestUndo : public QObject {
    Q_OBJECT

private slots:
    void testUndoRename();
};

void TestUndo::testUndoRename()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString original = tempDir.filePath(QStringLiteral("original.txt"));
    QString renamed = tempDir.filePath(QStringLiteral("renamed.txt"));

    QFile f(original);
    f.open(QIODevice::WriteOnly);
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

QTEST_MAIN(TestUndo)
#include "test_undo.moc"
