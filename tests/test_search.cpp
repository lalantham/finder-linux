#include "FileSystemModel.h"
#include "SearchEngine.h"
#include "SearchQuery.h"
#include <QFile>
#include <QItemSelectionModel>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestSearch : public QObject {
    Q_OBJECT

private slots:
    void testSearchMatching();
    void testSearchKindFiltering();
    void testQueryParser();
    void testContentSearch();
    void testContentOrNameSearch();
    void testThisPCRoots();
    void testModelInstantFilter();
    void testModelFilterByKind();
    void testSortKeepsCurrentItem();
    void testFoldersStayOnTopWhenDescending();
    void testAsyncLoading();
    void testChipExtraction();
    void testChipRemovalRoundTrip();
    void testRelevanceSorting();
    void testSearchResultsRefineFilter();
    void testDirectoryChangeDuringSearchKeepsResults();
};

void TestSearch::testSearchMatching() {
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

    QSignalSpy batchSpy(&engine, &SearchEngine::itemsFound);
    QSignalSpy finishedSpy(&engine, &SearchEngine::searchFinished);

    engine.startSearch(filter);
    QVERIFY(finishedSpy.wait(5000));

    // Both apple.txt and pineapple.txt should match
    int found = 0;
    for (const auto& args : batchSpy) {
        found += static_cast<int>(args.at(0).value<QVector<FileItem>>().size());
    }
    QCOMPARE(found, 2);
}

void TestSearch::testSearchKindFiltering() {
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

    QSignalSpy batchSpy(&engine, &SearchEngine::itemsFound);
    QSignalSpy finishedSpy(&engine, &SearchEngine::searchFinished);

    engine.startSearch(filter);
    QVERIFY(finishedSpy.wait(5000));

    int found = 0;
    for (const auto& args : batchSpy) {
        found += static_cast<int>(args.at(0).value<QVector<FileItem>>().size());
    }
    QCOMPARE(found, 1);
}

void TestSearch::testQueryParser() {
    const SearchFilter filter = SearchQuery::parse(
        QStringLiteral("annual report kind:image tag:Red,Work size:>1MB "
                       "modified:>2024-01-01 ext:png -draft content:\"annual report\""));

    const QStringList expectedName{QStringLiteral("annual"), QStringLiteral("report")};
    QCOMPARE(filter.nameTerms, expectedName);
    const QStringList expectedExclude{QStringLiteral("draft")};
    QCOMPARE(filter.excludeTerms, expectedExclude);
    const QStringList expectedContent{QStringLiteral("annual report")};
    QCOMPARE(filter.contentTerms, expectedContent);
    QCOMPARE(filter.kindFilter, QStringLiteral("images"));
    const QStringList expectedTags{QStringLiteral("Red"), QStringLiteral("Work")};
    QCOMPARE(filter.tags, expectedTags);
    const QStringList expectedExt{QStringLiteral("png")};
    QCOMPARE(filter.extensions, expectedExt);
    QCOMPARE(filter.minSize, 1000001LL);
    QCOMPARE(filter.modifiedFrom, QDate(2024, 1, 2));
    QVERIFY(filter.searchContents);

    const SearchFilter range =
        SearchQuery::parse(QStringLiteral("size:1MB..10MB modified:2024-01-01..2024-01-31"));
    QCOMPARE(range.minSize, 1000000LL);
    QCOMPARE(range.maxSize, 10000000LL);
    QCOMPARE(range.modifiedFrom, QDate(2024, 1, 1));
    QCOMPARE(range.modifiedTo, QDate(2024, 1, 31));

    const SearchFilter bare = SearchQuery::parse(QStringLiteral("http://example.com"));
    const QStringList expectedBare{QStringLiteral("http://example.com")};
    QCOMPARE(bare.nameTerms, expectedBare);
}

void TestSearch::testContentSearch() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QFile hit(tempDir.filePath(QStringLiteral("notes.txt")));
    QVERIFY(hit.open(QIODevice::WriteOnly));
    hit.write("The secret needle is in this file.");
    hit.close();

    QFile miss(tempDir.filePath(QStringLiteral("other.txt")));
    QVERIFY(miss.open(QIODevice::WriteOnly));
    miss.write("Nothing relevant here.");
    miss.close();

    SearchEngine engine;
    SearchFilter filter;
    filter.searchPath = tempDir.path();
    filter.recursive = false;
    filter.searchContents = true;
    filter.contentTerms = {QStringLiteral("needle")};

    QSignalSpy batchSpy(&engine, &SearchEngine::itemsFound);
    QSignalSpy finishedSpy(&engine, &SearchEngine::searchFinished);
    engine.startSearch(filter);
    QVERIFY(finishedSpy.wait(5000));

    QStringList found;
    for (const auto& args : batchSpy) {
        for (const auto& item : args.at(0).value<QVector<FileItem>>()) {
            found.append(item.fileName());
        }
    }
    QCOMPARE(found, QStringList{QStringLiteral("notes.txt")});
}

void TestSearch::testContentOrNameSearch() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QFile byContent(tempDir.filePath(QStringLiteral("notes.txt")));
    QVERIFY(byContent.open(QIODevice::WriteOnly));
    byContent.write("contains the word needle");
    byContent.close();

    QFile byName(tempDir.filePath(QStringLiteral("needle-name.txt")));
    QVERIFY(byName.open(QIODevice::WriteOnly));
    byName.write("nothing inside");
    byName.close();

    SearchEngine engine;
    SearchFilter filter;
    filter.searchPath = tempDir.path();
    filter.recursive = false;
    filter.searchContents = true;
    filter.nameTerms = {QStringLiteral("needle")};
    filter.contentTerms = {QStringLiteral("needle")};

    QSignalSpy batchSpy(&engine, &SearchEngine::itemsFound);
    QSignalSpy finishedSpy(&engine, &SearchEngine::searchFinished);
    engine.startSearch(filter);
    QVERIFY(finishedSpy.wait(5000));

    QStringList found;
    for (const auto& args : batchSpy) {
        for (const auto& item : args.at(0).value<QVector<FileItem>>()) {
            found.append(item.fileName());
        }
    }
    found.sort();
    const QStringList expected{QStringLiteral("needle-name.txt"), QStringLiteral("notes.txt")};
    QCOMPARE(found, expected);
}

void TestSearch::testThisPCRoots() {
    const QStringList roots = SearchEngine::localSearchRoots();
    QVERIFY(!roots.isEmpty());
    QVERIFY(roots.contains(QDir::homePath()));
}

void TestSearch::testModelInstantFilter() {
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
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(),
             QStringLiteral("report_alpha.pdf"));

    // Filter "report" -> 2 matches
    model.setFilterQuery(QStringLiteral("report"));
    QCOMPARE(model.rowCount(), 2);

    // Clear filter -> 3 items restored
    model.setFilterQuery(QString());
    QCOMPARE(model.rowCount(), 3);
}

void TestSearch::testModelFilterByKind() {
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
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(),
             QStringLiteral("folderA"));

    // Filter by Images
    model.setFilterQuery(QString(), QStringLiteral("Images"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(),
             QStringLiteral("photo.jpg"));

    // Filter by Documents
    model.setFilterQuery(QString(), QStringLiteral("Documents"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FileSystemModel::FileNameRole).toString(),
             QStringLiteral("document.pdf"));
}

void TestSearch::testSortKeepsCurrentItem() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    for (const QString& name :
         {QStringLiteral("alpha.txt"), QStringLiteral("mike.txt"), QStringLiteral("zulu.txt")}) {
        QFile f(tempDir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    FileSystemModel model;
    model.setRootPath(tempDir.path());
    QCOMPARE(model.rowCount(), 3);

    QItemSelectionModel selection(&model);
    const QModelIndex first = model.index(0, 0);
    const QString firstName = first.data(FileSystemModel::FileNameRole).toString();
    selection.setCurrentIndex(first, QItemSelectionModel::ClearAndSelect);
    QPersistentModelIndex persistent(first);

    model.sort(FileSystemModel::ColName, Qt::DescendingOrder);

    // Sorting must not reset the model or lose the selected item.
    QVERIFY(persistent.isValid());
    QCOMPARE(persistent.data(FileSystemModel::FileNameRole).toString(), firstName);
    QVERIFY(selection.currentIndex().isValid());
    QCOMPARE(selection.currentIndex().data(FileSystemModel::FileNameRole).toString(), firstName);

    QCOMPARE(model.index(0, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("zulu.txt"));
    QCOMPARE(model.index(2, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("alpha.txt"));
}

void TestSearch::testFoldersStayOnTopWhenDescending() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QDir(tempDir.path()).mkdir(QStringLiteral("zzz-folder"));
    for (const QString& name : {QStringLiteral("aaa.txt"), QStringLiteral("mmm.txt")}) {
        QFile f(tempDir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    FileSystemModel model;
    model.setRootPath(tempDir.path());
    QCOMPARE(model.rowCount(), 3);

    model.sort(FileSystemModel::ColName, Qt::DescendingOrder);

    // Descending order must not undo the folders-first rule.
    QCOMPARE(model.index(0, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("zzz-folder"));
    QCOMPARE(model.index(1, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("mmm.txt"));
    QCOMPARE(model.index(2, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("aaa.txt"));
}

void TestSearch::testAsyncLoading() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    for (const QString& name :
         {QStringLiteral("one.txt"), QStringLiteral("two.txt"), QStringLiteral("three.txt")}) {
        QFile f(tempDir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    struct AsyncGuard {
        AsyncGuard() {
            FileSystemModel::setAsyncLoadingEnabled(true);
        }
        ~AsyncGuard() {
            FileSystemModel::setAsyncLoadingEnabled(false);
        }
    } guard;

    FileSystemModel model;
    QSignalSpy loadingSpy(&model, &FileSystemModel::directoryLoading);
    QSignalSpy loadedSpy(&model, &FileSystemModel::directoryLoaded);

    model.setRootPath(tempDir.path());
    QVERIFY(loadedSpy.wait(5000));

    QCOMPARE(loadingSpy.count(), 1);
    QCOMPARE(model.rowCount(), 3);
}

void TestSearch::testChipExtraction() {
    const QString query =
        QStringLiteral("report kind:image tag:Red,Work -draft content:\"annual report\"");
    const QVector<SearchQuery::Chip> chips = SearchQuery::chips(query);
    QCOMPARE(chips.size(), 4);
    QCOMPARE(chips.at(0).token, QStringLiteral("kind:image"));
    QCOMPARE(chips.at(1).token, QStringLiteral("tag:Red,Work"));
    QCOMPARE(chips.at(2).token, QStringLiteral("-draft"));
    QVERIFY(chips.at(2).excluded);
    QCOMPARE(chips.at(3).token, QStringLiteral("content:\"annual report\""));
}

void TestSearch::testChipRemovalRoundTrip() {
    const QString query = QStringLiteral("report kind:image content:\"annual report\" -draft");
    const QString removed =
        SearchQuery::removeToken(query, QStringLiteral("content:\"annual report\""));
    QCOMPARE(removed, QStringLiteral("report kind:image -draft"));
    QCOMPARE(SearchQuery::tokenize(removed),
             QStringList({QStringLiteral("report"), QStringLiteral("kind:image"),
                          QStringLiteral("-draft")}));
    QCOMPARE(SearchQuery::removeToken(query, QStringLiteral("missing")), query);
}

void TestSearch::testRelevanceSorting() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVector<FileItem> items;
    for (const QString& name : {QStringLiteral("contains.txt"), QStringLiteral("exact.txt"),
                                QStringLiteral("prefix-other.txt")}) {
        QFile f(tempDir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        FileItem item(QFileInfo(tempDir.filePath(name)));
        item.setRelevance(name.startsWith(QStringLiteral("exact"))    ? 100
                          : name.startsWith(QStringLiteral("prefix")) ? 60
                                                                      : 30);
        items.append(item);
    }

    FileSystemModel model;
    model.setSearchResults(items);
    model.setSortByRelevance(true);
    model.sort(FileSystemModel::ColName, Qt::DescendingOrder);
    QCOMPARE(model.index(0, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("exact.txt"));
    QCOMPARE(model.index(1, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("prefix-other.txt"));
}

void TestSearch::testSearchResultsRefineFilter() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QVector<FileItem> items;
    for (const QString& name : {QStringLiteral("apple.txt"), QStringLiteral("banana.txt")}) {
        QFile f(tempDir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        items.append(FileItem(QFileInfo(tempDir.filePath(name))));
    }

    FileSystemModel model;
    model.setSearchResults(items);
    QCOMPARE(model.rowCount(), 2);
    model.setSearchResultsFilter(QStringLiteral("apple"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.index(0, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("apple.txt"));

    model.appendSearchResults(
        {FileItem(QFileInfo(tempDir.filePath(QStringLiteral("cherry-pie.txt"))))});
    QCOMPARE(model.rowCount(), 1); // non-matching append stays hidden

    model.setSearchResultsFilter(QString());
    QCOMPARE(model.rowCount(), 3);
}

void TestSearch::testDirectoryChangeDuringSearchKeepsResults() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QFile f(tempDir.filePath(QStringLiteral("keep.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    FileSystemModel model;
    model.setRootPath(tempDir.path());
    QVector<FileItem> results{FileItem(QFileInfo(
        f.fileName().isEmpty() ? QString() : tempDir.filePath(QStringLiteral("keep.txt"))))};
    model.setSearchResults(results);
    QCOMPARE(model.rowCount(), 1);
    QSignalSpy changeSpy(&model, &FileSystemModel::rootDirectoryChanged);

    QVERIFY(QMetaObject::invokeMethod(&model, "onDirectoryChanged", Qt::DirectConnection,
                                      Q_ARG(QString, tempDir.path())));
    QCOMPARE(changeSpy.count(), 1);
    QCOMPARE(model.rowCount(), 1);
}

QTEST_MAIN(TestSearch)
#include "test_search.moc"
