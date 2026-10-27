#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include "SearchEngine.h"

class TestSearch : public QObject {
    Q_OBJECT

private slots:
    void testSearchMatching();
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

QTEST_MAIN(TestSearch)
#include "test_search.moc"
