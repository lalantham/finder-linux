#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include "XdgTrash.h"

class TestXdgTrash : public QObject {
    Q_OBJECT

private slots:
    void testTrashAndRestore();
    void testEmptyTrash();
    void testTrashInfoRoundTrip();
    void testRestoreConflictKeepsBoth();
    void testPurgeOlderThan();
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

void TestXdgTrash::testTrashInfoRoundTrip()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    XdgTrash::setCustomTrashDirectory(tempDir.path() + QStringLiteral("/Trash"));

    // A path with a space and a percent sign exercises percent-encoding.
    const QString testFile = tempDir.filePath(QStringLiteral("my 100% file.txt"));
    QFile f(testFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("payload");
    f.close();

    QString err;
    QVERIFY2(XdgTrash::moveToTrash(testFile, &err), qPrintable(err));
    QVERIFY(!QFile::exists(testFile));
    QVERIFY(QFile::exists(XdgTrash::trashInfoDirectory()));

    QVector<TrashItem> items = XdgTrash::listTrashItems();
    QCOMPARE(items.size(), 1);
    const TrashItem& item = items.first();

    // The listed original path must be decoded exactly once.
    QCOMPARE(item.originalPath, testFile);
    QVERIFY(item.deletionDate.isValid());
    QVERIFY(qAbs(item.deletionDate.secsTo(QDateTime::currentDateTime())) < 300);

    // DeletionDate must use the 24-hour format required by the XDG spec.
    QFile info(item.trashInfoPath);
    QVERIFY(info.open(QIODevice::ReadOnly | QIODevice::Text));
    QString dateLine;
    while (!info.atEnd()) {
        const QString line = QString::fromUtf8(info.readLine()).trimmed();
        if (line.startsWith(QStringLiteral("DeletionDate="))) {
            dateLine = line.mid(QStringLiteral("DeletionDate=").size());
            break;
        }
    }
    info.close();
    QVERIFY(!dateLine.isEmpty());
    QVERIFY2(QDateTime::fromString(dateLine, QStringLiteral("yyyy-MM-ddTHH:mm:ss")).isValid(),
             qPrintable(dateLine));

    QVERIFY2(XdgTrash::restoreItem(item.trashFilePath, &err), qPrintable(err));
    QVERIFY(QFile::exists(testFile));
    QVERIFY(!QFile::exists(item.trashInfoPath));
}

void TestXdgTrash::testRestoreConflictKeepsBoth()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    XdgTrash::setCustomTrashDirectory(tempDir.path() + QStringLiteral("/Trash"));

    const QString testFile = tempDir.filePath(QStringLiteral("conflict.txt"));
    {
        QFile file(testFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original");
    }

    QString error;
    QVERIFY2(XdgTrash::moveToTrash(testFile, &error), qPrintable(error));
    QVERIFY(!QFile::exists(testFile));

    // Recreate a file at the original path.
    {
        QFile file(testFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("replacement");
    }

    const QVector<TrashItem> items = XdgTrash::listTrashItems();
    QCOMPARE(items.size(), 1);

    QString restoredPath;
    QVERIFY2(XdgTrash::restoreItem(items.first().trashFilePath, &error, &restoredPath),
             qPrintable(error));
    QVERIFY(QFile::exists(testFile));
    QVERIFY(!restoredPath.isEmpty());
    QVERIFY(restoredPath != testFile);
    QVERIFY(QFile::exists(restoredPath));
    QCOMPARE(QFileInfo(restoredPath).fileName(), QStringLiteral("conflict (restored).txt"));

    QFile original(testFile);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("replacement"));

    QFile restored(restoredPath);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("original"));
}

void TestXdgTrash::testPurgeOlderThan()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    XdgTrash::setCustomTrashDirectory(tempDir.path() + QStringLiteral("/Trash"));

    const QString oldFile = tempDir.filePath(QStringLiteral("old.txt"));
    const QString newFile = tempDir.filePath(QStringLiteral("new.txt"));
    for (const QString& path : {oldFile, newFile}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("payload");
    }

    QString error;
    QVERIFY2(XdgTrash::moveToTrash(oldFile, &error), qPrintable(error));
    QVERIFY2(XdgTrash::moveToTrash(newFile, &error), qPrintable(error));

    QVector<TrashItem> items = XdgTrash::listTrashItems();
    QCOMPARE(items.size(), 2);

    QString oldInfoPath;
    for (const TrashItem& item : items) {
        if (item.originalPath == oldFile) {
            oldInfoPath = item.trashInfoPath;
            break;
        }
    }
    QVERIFY(!oldInfoPath.isEmpty());

    QFile info(oldInfoPath);
    QVERIFY(info.open(QIODevice::ReadOnly | QIODevice::Text));
    QString contents = QString::fromUtf8(info.readAll());
    info.close();
    const QString oldDate = QDateTime::currentDateTime().addDays(-10)
                                .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    const QStringList lines = contents.split(QLatin1Char('\n'));
    QStringList updated;
    for (const QString& line : lines) {
        updated.append(line.startsWith(QStringLiteral("DeletionDate="))
            ? QStringLiteral("DeletionDate=") + oldDate
            : line);
    }
    QVERIFY(info.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    info.write(updated.join(QLatin1Char('\n')).toUtf8());
    info.close();

    const int purged = XdgTrash::purgeOlderThan(
        QDateTime::currentDateTime().addDays(-5), &error);
    QCOMPARE(purged, 1);

    items = XdgTrash::listTrashItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().originalPath, newFile);
}

QTEST_MAIN(TestXdgTrash)
#include "test_xdgtrash.moc"
