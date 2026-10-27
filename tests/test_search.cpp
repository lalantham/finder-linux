#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include "SearchEngine.h"
#include "FileSystemModel.h"

class TestSearch : public QObject {
    Q_OBJECT

private slots:
    void testSearchMatching();
    void testSearchKindFiltering();
    void testModelInstantFilter();
    void testModelFilterByKind();
};

void TestSearch::testSearchMatching()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    // Create a directory tree
    QFile f1(tempDir.filePath(QStringLiteral("apple.txt")));
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write("apple");
    f1.close();

    QFile f2(tempDir.filePath(QStringLiteral("banana.log")));
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("banana");
    f2.close();

    QDir(tempDir.path()).mkdir(QStringLiteral("sub"));
    QFile f3(tempDir.filePath(QStringLiteral("sub/pineapple.txt")));
    QVERIFY(f3.open(QIODevice::WriteOnly));
    f3.write("pineapple");
    f3.close();

    SearchEngine engine;
    SearchFilter filter;
    filter.searchPath = tempDir.path();
    filter.query = QStringLiteral("apple");
    filter.recursive = true;

    QSignalSpy itemSpy(&engine, &SearchEngine::itemFound);
    QSignalSpy finishedSpy(&engine, &SearchEngine::searchFinished);

    engine.startSearch(filter);
    QVERIFY(finishedSpy.wait(5000));

    // Both apple.txt and pineapple.txt should match
    QCOMPARE(itemSpy.count(), 2);
}

void TestSearch::testSearchKindFiltering()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QFile f1(tempDir.filePath(QStringLiteral("image1.png")));
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write("img");
    f1.close();

    QFile f2(tempDir.filePath(QStringLiteral("notes.txt")));
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("doc");
    f2.close();

    SearchEngine engine;
    SearchFilter filter;
    filter.searchPath = tempDir.path();
    filter.kindFilter = QStringLiteral("Images");
    filter.recursive = false;

    QSignalSpy itemSpy(&engine, &SearchEngine::itemFound);
    QSignalSpy finishedSpy(&engine, &SearchEngine::searchFinished);

    engine.startSearch(filter);
    QVERIFY(finishedSpy.wait(5000));

    QCOMPARE(itemSpy.count(), 1);
}

void TestSearch::testModelInstantFilter()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QFile f1(tempDir.filePath(QStringLiteral("report_alpha.pdf")));
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write("alpha");
    f1.close();

    QFile f2(tempDir.filePath(QStringLiteral("report_beta.pdf")));
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("beta");
    f2.close();

    QFile f3(tempDir.filePath(QStringLiteral("summary.txt")));
    QVERIFY(f3.open(QIODevice::WriteOnly));
    f3.write("summary");
    f3.close();

    FileSystemModel model;
    model.setRootPath(tempDir.path());
    QCOMPARE(model.rowCount(), 3);

    // Instant filter "alpha"
    model.setFilterQuery(QStringLiteral("alpha"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(), QStringLiteral("report_alpha.pdf"));

    // Filter "report" -> 2 matches
    model.setFilterQuery(QStringLiteral("report"));
    QCOMPARE(model.rowCount(), 2);

    // Clear filter -> 3 items restored
    model.setFilterQuery(QString());
    QCOMPARE(model.rowCount(), 3);
}

void TestSearch::testModelFilterByKind()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QFile f1(tempDir.filePath(QStringLiteral("photo.jpg")));
    QVERIFY(f1.open(QIODevice::WriteOnly));
    f1.write("photo");
    f1.close();

    QFile f2(tempDir.filePath(QStringLiteral("document.pdf")));
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("doc");
    f2.close();

    QDir(tempDir.path()).mkdir(QStringLiteral("folderA"));

    FileSystemModel model;
    model.setRootPath(tempDir.path());
    QCOMPARE(model.rowCount(), 3);

    // Filter by Folders
    model.setFilterQuery(QString(), QStringLiteral("Folders"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(), QStringLiteral("folderA"));

    // Filter by Images
    model.setFilterQuery(QString(), QStringLiteral("Images"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(), QStringLiteral("photo.jpg"));

    // Filter by Documents
    model.setFilterQuery(QString(), QStringLiteral("Documents"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(), QStringLiteral("document.pdf"));
}

QTEST_MAIN(TestSearch)
#include "test_search.moc"
