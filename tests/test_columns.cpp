#include "AppSettings.h"
#include "DiskImageManager.h"
#include "FileItem.h"
#include "FileMetadataManager.h"
#include "FileSystemModel.h"
#include "MediaMetadata.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

class TestColumns : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testMetadataColumns();
    void testNaturalSort();
    void testSortBySizeAndDates();
    void testViewOptionsPersistence();
    void testSmartFolderPersistence();
    void testRecentLocationsPersistence();
    void testDiskImageDetection();
    void testDiskImageMountIsAsync();
    void testDateAddedMetadata();
    void testMetadataStorePruning();
    void testMetadataSaveIsFast();
    void testMediaMetadata();
    void testCommentXattr();

private:
    QTemporaryDir m_settingsDir;
};

void TestColumns::initTestCase() {
    QVERIFY(m_settingsDir.isValid());
    AppSettings::setSettingsFilePath(m_settingsDir.filePath(QStringLiteral("columns.ini")));
}

void TestColumns::cleanupTestCase() {
    AppSettings::setSettingsFilePath(QString());
}

void TestColumns::testMetadataColumns() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("note.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();

    FileItem item(path);
    const bool commentSupported = item.setComment(QStringLiteral("Review later"));

    FileSystemModel model;
    model.setDirectoriesFirst(false);
    model.setRootPath(dir.path());

    const QModelIndex idx = model.indexForPath(path);
    QVERIFY(idx.isValid());
    const int row = idx.row();

    QCOMPARE(model.headerData(FileSystemModel::ColDateCreated, Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             QStringLiteral("Date Created"));
    QCOMPARE(model.headerData(FileSystemModel::ColDateOpened, Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             QStringLiteral("Date Last Opened"));
    QCOMPARE(model.headerData(FileSystemModel::ColPermissions, Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             QStringLiteral("Permissions"));

    QVERIFY(!model.data(model.index(row, FileSystemModel::ColOwner), Qt::DisplayRole)
                 .toString()
                 .isEmpty());
    QVERIFY(!model.data(model.index(row, FileSystemModel::ColGroup), Qt::DisplayRole)
                 .toString()
                 .isEmpty());
    QVERIFY(model.data(model.index(row, FileSystemModel::ColPermissions), Qt::DisplayRole)
                .toString()
                .contains(QLatin1Char('r')));
    if (commentSupported) {
        QCOMPARE(
            model.data(model.index(row, FileSystemModel::ColComment), Qt::DisplayRole).toString(),
            QStringLiteral("Review later"));
        QCOMPARE(model
                     .data(model.index(row, FileSystemModel::ColComment),
                           FileSystemModel::FileCommentRole)
                     .toString(),
                 QStringLiteral("Review later"));
    }
    QVERIFY(model
                .data(model.index(row, FileSystemModel::ColDateOpened),
                      FileSystemModel::FileDateOpenedRole)
                .toDateTime()
                .isValid());
    QVERIFY(model
                .data(model.index(row, FileSystemModel::ColDateCreated),
                      FileSystemModel::FileDateCreatedRole)
                .toDateTime()
                .isValid());
}

void TestColumns::testNaturalSort() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const QString& name : {QStringLiteral("file10.txt"), QStringLiteral("file2.txt"),
                                QStringLiteral("file1.txt"), QStringLiteral("File20.txt")}) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    FileSystemModel model;
    model.setDirectoriesFirst(false);
    model.setRootPath(dir.path());
    model.sort(FileSystemModel::ColName, Qt::AscendingOrder);

    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        names.append(model.index(row, FileSystemModel::ColName).data(Qt::DisplayRole).toString());
    }
    const QStringList expected{QStringLiteral("file1.txt"), QStringLiteral("file2.txt"),
                               QStringLiteral("file10.txt"), QStringLiteral("File20.txt")};
    QCOMPARE(names, expected);
}

void TestColumns::testSortBySizeAndDates() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto writeFile = [&dir](const QString& name, const QByteArray& data) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(data);
        f.close();
        return true;
    };
    QVERIFY(writeFile(QStringLiteral("small.txt"), "1"));
    QVERIFY(writeFile(QStringLiteral("large.txt"), QByteArray(4096, 'x')));

    FileSystemModel model;
    model.setDirectoriesFirst(false);
    model.setRootPath(dir.path());

    model.sort(FileSystemModel::ColSize, Qt::AscendingOrder);
    QCOMPARE(model.index(0, FileSystemModel::ColName).data(Qt::DisplayRole).toString(),
             QStringLiteral("small.txt"));
    model.sort(FileSystemModel::ColSize, Qt::DescendingOrder);
    QCOMPARE(model.index(0, FileSystemModel::ColName).data(Qt::DisplayRole).toString(),
             QStringLiteral("large.txt"));

    // Metadata columns must be sortable without crashing and keep valid rows.
    for (const int column :
         {FileSystemModel::ColDateCreated, FileSystemModel::ColDateOpened,
          FileSystemModel::ColPermissions, FileSystemModel::ColOwner, FileSystemModel::ColGroup,
          FileSystemModel::ColComment, FileSystemModel::ColTags, FileSystemModel::ColKind}) {
        model.sort(column, Qt::AscendingOrder);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.index(0, FileSystemModel::ColName).isValid());
    }
}

void TestColumns::testViewOptionsPersistence() {
    AppSettings& settings = AppSettings::instance();

    FolderViewOptions defaults;
    defaults.iconSize = 96;
    defaults.textSize = 12;
    defaults.labelPosition = 1;
    defaults.gridSpacing = 18;
    defaults.showItemInfo = true;
    defaults.showIconPreview = false;
    defaults.manualArrangement = true;
    settings.setDefaultViewOptions(defaults);
    settings.setListVisibleColumns(
        {QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("owner")});
    settings.setShowSidebar(false);
    settings.setShowStatusBar(false);

    FolderViewOptions custom = defaults;
    custom.iconSize = 32;
    custom.textSize = 9;
    custom.labelPosition = 0;
    custom.gridSpacing = 4;
    custom.showItemInfo = false;
    custom.showIconPreview = true;
    custom.manualArrangement = false;
    settings.setViewOptions(QStringLiteral("/tmp/folder-options-test"), custom);
    QVariantMap positions;
    positions.insert(QStringLiteral("icon-a.txt"), QPoint(12, 34));
    settings.setIconPositions(QStringLiteral("/tmp/folder-options-test"), positions);

    // Reload from disk to verify round-tripping.
    AppSettings::setSettingsFilePath(m_settingsDir.filePath(QStringLiteral("columns.ini")));

    const FolderViewOptions reloaded =
        settings.viewOptions(QStringLiteral("/tmp/folder-options-test"));
    QCOMPARE(reloaded.iconSize, 32);
    QCOMPARE(reloaded.textSize, 9);
    QCOMPARE(reloaded.labelPosition, 0);
    QCOMPARE(reloaded.gridSpacing, 4);
    QCOMPARE(reloaded.showItemInfo, false);
    QCOMPARE(reloaded.showIconPreview, true);
    QCOMPARE(reloaded.manualArrangement, false);

    const FolderViewOptions fallback = settings.viewOptions(QStringLiteral("/tmp/no-overrides"));
    QCOMPARE(fallback.iconSize, 96);
    QCOMPARE(fallback.textSize, 12);
    QCOMPARE(fallback.labelPosition, 1);
    QCOMPARE(fallback.gridSpacing, 18);
    QCOMPARE(fallback.showItemInfo, true);
    QCOMPARE(fallback.showIconPreview, false);
    QCOMPARE(fallback.manualArrangement, true);

    const QStringList expectedColumns{QStringLiteral("name"), QStringLiteral("size"),
                                      QStringLiteral("owner")};
    QCOMPARE(settings.listVisibleColumns(), expectedColumns);
    QCOMPARE(settings.showSidebar(), false);
    QCOMPARE(settings.showToolbar(), true);
    QCOMPARE(settings.showStatusBar(), false);

    const QVariantMap reloadedPositions =
        settings.iconPositions(QStringLiteral("/tmp/folder-options-test"));
    QCOMPARE(reloadedPositions.value(QStringLiteral("icon-a.txt")).toPoint(), QPoint(12, 34));
}

void TestColumns::testSmartFolderPersistence() {
    AppSettings& settings = AppSettings::instance();

    SmartFolder folder;
    folder.name = QStringLiteral("Large recent images");
    folder.filter =
        SearchQuery::parse(QStringLiteral("kind:image size:>1MB tag:Red content:invoice"));
    folder.filter.thisPC = true;
    folder.filter.includeHidden = true;
    folder.filter.modifiedFrom = QDate(2025, 1, 1);
    folder.filter.extensions = {QStringLiteral("png"), QStringLiteral("jpg")};
    settings.saveSmartFolder(folder);

    AppSettings::setSettingsFilePath(m_settingsDir.filePath(QStringLiteral("columns.ini")));
    const SmartFolder reloaded = settings.smartFolder(QStringLiteral("large recent IMAGES"));
    QVERIFY(reloaded.isValid());
    QCOMPARE(reloaded.filter.kindFilter, QStringLiteral("images"));
    QVERIFY(reloaded.filter.thisPC);
    QVERIFY(reloaded.filter.includeHidden);
    QCOMPARE(reloaded.filter.minSize, 1000001LL);
    const QStringList expectedTags{QStringLiteral("Red")};
    QCOMPARE(reloaded.filter.tags, expectedTags);
    QVERIFY(reloaded.filter.searchContents);
    QCOMPARE(reloaded.filter.modifiedFrom, QDate(2025, 1, 1));
    const QStringList expectedExtensions{QStringLiteral("png"), QStringLiteral("jpg")};
    QCOMPARE(reloaded.filter.extensions, expectedExtensions);

    settings.removeSmartFolder(QStringLiteral("Large recent images"));
    QVERIFY(!settings.smartFolder(QStringLiteral("Large recent images")).isValid());
}

void TestColumns::testRecentLocationsPersistence() {
    AppSettings& settings = AppSettings::instance();
    settings.addRecentFolder(QStringLiteral("/tmp/one"));
    settings.addRecentFolder(QStringLiteral("/tmp/two"));
    settings.addRecentServer(QStringLiteral("sftp://example.test/home"));

    AppSettings::setSettingsFilePath(m_settingsDir.filePath(QStringLiteral("columns.ini")));
    QCOMPARE(settings.recentFolders().first(), QStringLiteral("/tmp/two"));
    QVERIFY(settings.recentFolders().contains(QStringLiteral("/tmp/one")));
    QCOMPARE(settings.recentServers().first(), QStringLiteral("sftp://example.test/home"));

    settings.clearRecentFolders();
    settings.clearRecentServers();
    QVERIFY(settings.recentFolders().isEmpty());
    QVERIFY(settings.recentServers().isEmpty());
}

void TestColumns::testDiskImageDetection() {
    QVERIFY(DiskImageManager::isDiskImage(QStringLiteral("image.iso")));
    QVERIFY(DiskImageManager::isDiskImage(QStringLiteral("image.IMG")));
    QVERIFY(DiskImageManager::isDiskImage(QStringLiteral("image.raw")));
    QVERIFY(DiskImageManager::isDiskImage(QStringLiteral("image.dmg")));
    QVERIFY(DiskImageManager::isDiskImage(QStringLiteral("image.squashfs")));
    QVERIFY(!DiskImageManager::isDiskImage(QStringLiteral("image.zip")));
    QVERIFY(!DiskImageManager::isDiskImage(QStringLiteral("image.txt")));
}

void TestColumns::testDiskImageMountIsAsync() {
    // Mounting must never block the caller: a double-click on an ISO used to
    // freeze the window until udisksctl returned.
    //
    // The key observable is *ordering*: an async implementation returns before
    // the callback runs, while a blocking implementation has already invoked it
    // by the time the call returns. That distinction holds regardless of how
    // fast the underlying work happens to fail.
    bool callReturned = false;
    bool calledDuringCall = false;
    bool called = false;

    DiskImageManager::mountImageAsync(QStringLiteral("/nonexistent/missing-image.iso"),
                                      [&](bool, const QString&, const QString&) {
                                          calledDuringCall = calledDuringCall || !callReturned;
                                          called = true;
                                      });
    callReturned = true;

    QVERIFY2(!calledDuringCall, "mountImageAsync ran its callback synchronously");
    QVERIFY(!called);

    // The callback is still delivered later, on the event loop.
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);

    // An empty path is rejected without any work.
    bool emptyRejected = false;
    DiskImageManager::mountImageAsync(
        QString(),
        [&emptyRejected](bool ok, const QString&, const QString&) { emptyRejected = !ok; });
    QVERIFY(emptyRejected);
}

void TestColumns::testDateAddedMetadata() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));
    FileMetadataManager::setStoragePathForTesting(metadataPath);

    const QString filePath = dir.filePath(QStringLiteral("tracked.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    FileSystemModel model;
    model.setRootPath(dir.path());
    const QModelIndex idx = model.indexForPath(filePath);
    QVERIFY(idx.isValid());
    const QVariant added = model.data(idx, FileSystemModel::FileDateAddedRole);
    QVERIFY(added.toDateTime().isValid());
    QVERIFY(!model.data(model.index(idx.row(), FileSystemModel::ColDateAdded), Qt::DisplayRole)
                 .toString()
                 .isEmpty());

    FileMetadataManager::instance().markOpened(filePath);
    QVERIFY(FileMetadataManager::instance().lastOpened(filePath).isValid());
    FileMetadataManager::instance().save();
    FileMetadataManager::setStoragePathForTesting(metadataPath);
    QVERIFY(FileMetadataManager::instance().dateAdded(filePath).isValid());
    QVERIFY(FileMetadataManager::instance().lastOpened(filePath).isValid());
}

void TestColumns::testMetadataStorePruning() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));
    FileMetadataManager::setStoragePathForTesting(metadataPath);
    auto& manager = FileMetadataManager::instance();

    // Two real files and two paths that never existed. Browsing is what feeds
    // this store, so stale entries are the normal case, not an edge case.
    QStringList real;
    for (int i = 0; i < 2; ++i) {
        const QString path = dir.filePath(QStringLiteral("present%1.txt").arg(i));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        real << path;
    }
    const QStringList stale{dir.filePath(QStringLiteral("removed-1.txt")),
                            dir.filePath(QStringLiteral("removed-2.txt"))};

    manager.registerPaths(real);
    manager.registerPaths(stale);
    manager.save();

    const int removed = manager.pruneMissingEntries();
    QCOMPARE(removed, 2);
    for (const QString& path : stale) {
        QVERIFY(!manager.dateAdded(path).isValid());
    }
    for (const QString& path : real) {
        QVERIFY(manager.dateAdded(path).isValid());
    }

    // The trimmed store must survive a round-trip through disk.
    manager.save();
    FileMetadataManager::setStoragePathForTesting(metadataPath);
    for (const QString& path : stale) {
        QVERIFY(!FileMetadataManager::instance().dateAdded(path).isValid());
    }
    for (const QString& path : real) {
        QVERIFY(FileMetadataManager::instance().dateAdded(path).isValid());
    }

    // Pruning a clean store removes nothing.
    QCOMPARE(FileMetadataManager::instance().pruneMissingEntries(), 0);

    // The explicit cap keeps the newest entries when the store is oversized.
    // All entries are registered in the same millisecond here, so just assert
    // that the cap is honoured rather than which specific rows survive.
    const int capped = FileMetadataManager::instance().pruneMissingEntries(1);
    QVERIFY(capped >= 1);
}

void TestColumns::testMetadataSaveIsFast() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString metadataPath = dir.filePath(QStringLiteral("metadata.json"));
    FileMetadataManager::setStoragePathForTesting(metadataPath);
    auto& manager = FileMetadataManager::instance();

    // A store of this size used to take hundreds of milliseconds per save
    // because QJsonObject keeps keys sorted and the entries were inserted in
    // hash order, making every insert an O(n) memmove. Guard against that
    // quadratic regression: at 20k entries the old code needs ~150 ms while the
    // sorted build needs a couple of milliseconds.
    QStringList paths;
    paths.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        paths << dir.filePath(QStringLiteral("entry-%1.dat").arg(i));
    }
    manager.registerPaths(paths);

    QElapsedTimer timer;
    timer.start();
    manager.save();
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(elapsed < 60,
             qPrintable(QStringLiteral("saving 20000 entries took %1 ms; the store save looks "
                                       "quadratic again")
                            .arg(elapsed)));

    // The data must still round-trip exactly.
    FileMetadataManager::setStoragePathForTesting(metadataPath);
    QVERIFY(FileMetadataManager::instance().dateAdded(paths.first()).isValid());
    QVERIFY(FileMetadataManager::instance().dateAdded(paths.last()).isValid());
}

void TestColumns::testMediaMetadata() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("image.png"));
    QImage image(3, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));

    const MediaMetadata metadata = MediaMetadataReader::inspect(path);
    QVERIFY(metadata.dimensions.contains(QStringLiteral("3")));
    QVERIFY(metadata.dimensions.contains(QStringLiteral("2")));
}

void TestColumns::testCommentXattr() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("comment.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    FileItem item(path);
    if (!item.setComment(QStringLiteral("first"))) {
        QSKIP("Filesystem does not support user extended attributes");
    }
    QCOMPARE(item.comment(), QStringLiteral("first"));

    FileItem reloaded(path);
    QCOMPARE(reloaded.comment(), QStringLiteral("first"));

    QVERIFY(reloaded.setComment(QString()));
    FileItem cleared(path);
    QCOMPARE(cleared.comment(), QString());
}

QTEST_MAIN(TestColumns)
#include "test_columns.moc"
