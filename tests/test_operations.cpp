#include "FileOperationEngine.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>

class TestOperations : public QObject {
    Q_OBJECT

private slots:
    void testCreateFolderAndFile();
    void testRenameItem();
    void testCopyFile();
    void testMoveFile();
    void testDirectoryContainmentGuard();
    void testBrokenSymlinkOperations();
    void testCopyOntoSelfKeepsSource();
    void testMoveOntoSelfKeepsSource();
    void testReplaceExistingFileIsComplete();
    void testSkipIsNotReportedAsMoved();
    void testCopyFifoDoesNotHang();
    void testCopyPreservesDirectoryMode();
    void testCopyPreservesFileMetadata();
    void testOperationsAreQueued();
    void testCopyFolderMerge();
    void testMoveFolderMerge();
    void testPauseAndResumeState();
    void testOperationItemStatuses();
    void testCancelPendingItem();
};

void TestOperations::testCreateFolderAndFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FileOperationEngine engine;
    QString createdFolder, error;
    bool ok =
        engine.createFolder(tempDir.path(), QStringLiteral("MyFolder"), &createdFolder, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QDir(createdFolder).exists());

    QString createdFile;
    ok = engine.createFile(createdFolder, QStringLiteral("notes.txt"), &createdFile, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFile::exists(createdFile));
}

void TestOperations::testRenameItem() {
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

void TestOperations::testCopyFile() {
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

void TestOperations::testMoveFile() {
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

void TestOperations::testDirectoryContainmentGuard() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString parent = tempDir.filePath(QStringLiteral("parent"));
    QString child = parent + QStringLiteral("/subfolder");
    QVERIFY(QDir().mkpath(child));

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);

    // Attempting to move parent into child must fail
    engine.moveItems(QStringList{parent}, child, ConflictResolution::Replace);
    QVERIFY(spy.wait(5000));
    QList<QVariant> arguments = spy.takeFirst();
    OperationResult res = arguments.at(0).value<OperationResult>();
    QVERIFY(!res.success);
    QVERIFY(QDir(parent).exists());

    // Attempting to copy parent into child must fail
    engine.copyItems(QStringList{parent}, child, ConflictResolution::Replace);
    QVERIFY(spy.wait(5000));
    arguments = spy.takeFirst();
    res = arguments.at(0).value<OperationResult>();
    QVERIFY(!res.success);
}

void TestOperations::testBrokenSymlinkOperations() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString brokenTarget = tempDir.filePath(QStringLiteral("non_existent_target.txt"));
    QString linkPath = tempDir.filePath(QStringLiteral("broken_link"));
    QVERIFY(QFile::link(brokenTarget, linkPath));
    QVERIFY(QFileInfo(linkPath).isSymLink());
    QVERIFY(!QFileInfo::exists(brokenTarget));

    // Rename broken symlink
    FileOperationEngine engine;
    QString renamedLink, error;
    bool ok =
        engine.renameItem(linkPath, QStringLiteral("broken_link_renamed"), &renamedLink, &error);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(QFileInfo(renamedLink).isSymLink());
    QCOMPARE(QFileInfo(renamedLink).symLinkTarget(), brokenTarget);

    // Move broken symlink
    QString subDir = tempDir.filePath(QStringLiteral("subdir"));
    QVERIFY(QDir().mkpath(subDir));

    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    engine.moveItems(QStringList{renamedLink}, subDir, ConflictResolution::Replace);
    QVERIFY(spy.wait(5000));

    QString movedLink = subDir + QStringLiteral("/broken_link_renamed");
    QVERIFY(QFileInfo(movedLink).isSymLink());
    QCOMPARE(QFileInfo(movedLink).symLinkTarget(), brokenTarget);
}

static OperationResult waitForOperation(FileOperationEngine& engine, QSignalSpy& spy) {
    if (spy.isEmpty()) {
        spy.wait(5000);
    }
    if (spy.isEmpty()) {
        return OperationResult{};
    }
    return qvariant_cast<OperationResult>(spy.takeFirst().at(0));
}

static void writeFile(const QString& path, const QByteArray& content) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();
}

static QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

void TestOperations::testCopyOntoSelfKeepsSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("same.txt"));
    writeFile(file, "keep me");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({file}, dir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);

    QVERIFY2(QFile::exists(file), "copying an item onto itself must not delete it");
    QCOMPARE(readFile(file), QByteArray("keep me"));
    QVERIFY(res.skippedCount >= 1);
}

void TestOperations::testMoveOntoSelfKeepsSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("same.txt"));
    writeFile(file, "keep me");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.moveItems({file}, dir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);

    QVERIFY2(QFile::exists(file), "moving an item onto itself must not delete it");
    QCOMPARE(readFile(file), QByteArray("keep me"));
    QVERIFY(res.skippedCount >= 1);
    QVERIFY(res.movedPairs.isEmpty());
}

void TestOperations::testReplaceExistingFileIsComplete() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    const QString src = srcDir.filePath(QStringLiteral("data.txt"));
    writeFile(src, "new content");
    const QString dst = dstDir.filePath(QStringLiteral("data.txt"));
    writeFile(dst, "old content");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({src}, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);

    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QVERIFY(QFile::exists(src));
    QCOMPARE(readFile(dst), QByteArray("new content"));

    const auto leftovers =
        QDir(dstDir.path())
            .entryList({QStringLiteral(".finder_tmp_*")}, QDir::Files | QDir::Hidden);
    QVERIFY2(leftovers.isEmpty(), "temporary copy files must be cleaned up");
}

void TestOperations::testSkipIsNotReportedAsMoved() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    const QString copySrc = srcDir.filePath(QStringLiteral("copy.txt"));
    writeFile(copySrc, "source");
    const QString copyDst = dstDir.filePath(QStringLiteral("copy.txt"));
    writeFile(copyDst, "existing");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({copySrc}, dstDir.path(), ConflictResolution::Skip));
    const OperationResult copyRes = waitForOperation(engine, spy);
    QCOMPARE(copyRes.processedCount, 0);
    QCOMPARE(copyRes.skippedCount, 1);
    QVERIFY(copyRes.createdPaths.isEmpty());
    QCOMPARE(readFile(copyDst), QByteArray("existing"));

    const QString moveSrc = srcDir.filePath(QStringLiteral("move.txt"));
    writeFile(moveSrc, "source");
    const QString moveDst = dstDir.filePath(QStringLiteral("move.txt"));
    writeFile(moveDst, "existing");

    QVERIFY(engine.moveItems({moveSrc}, dstDir.path(), ConflictResolution::Skip));
    const OperationResult moveRes = waitForOperation(engine, spy);
    QVERIFY(moveRes.movedPairs.isEmpty());
    QVERIFY(QFile::exists(moveSrc));
    QVERIFY(QFile::exists(moveDst));
    QCOMPARE(readFile(moveDst), QByteArray("existing"));
}

void TestOperations::testCopyFifoDoesNotHang() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    const QString tree = srcDir.filePath(QStringLiteral("tree"));
    QVERIFY(QDir().mkpath(tree));
    writeFile(tree + QStringLiteral("/plain.txt"), "data");

    const QString fifoPath = tree + QStringLiteral("/pipe");
    QVERIFY(::mkfifo(QFile::encodeName(fifoPath).constData(), 0644) == 0);

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({tree}, dstDir.path(), ConflictResolution::Replace));
    QVERIFY2(spy.wait(5000), "copying a folder containing a FIFO must not block");
    const OperationResult res = qvariant_cast<OperationResult>(spy.takeFirst().at(0));

    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QVERIFY(QFile::exists(dstDir.path() + QStringLiteral("/tree/plain.txt")));

    struct stat st{};
    QVERIFY(::lstat(QFile::encodeName(dstDir.path() + QStringLiteral("/tree/pipe")).constData(),
                    &st) == 0);
    QVERIFY2(S_ISFIFO(st.st_mode), "FIFO must be recreated, not opened and streamed");
}

void TestOperations::testCopyPreservesDirectoryMode() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    const QString tree = srcDir.filePath(QStringLiteral("private"));
    QVERIFY(QDir().mkpath(tree));
    writeFile(tree + QStringLiteral("/inside.txt"), "secret");
    QVERIFY(::chmod(QFile::encodeName(tree).constData(), 0700) == 0);

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({tree}, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);
    QVERIFY2(res.success, qPrintable(res.errorMessage));

    struct stat st{};
    QVERIFY(::lstat(QFile::encodeName(dstDir.path() + QStringLiteral("/private")).constData(),
                    &st) == 0);
    QCOMPARE(static_cast<int>(st.st_mode & 0777), 0700);
}

void TestOperations::testCopyPreservesFileMetadata() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    const QString srcFile = srcDir.filePath(QStringLiteral("metadata.txt"));
    QFile file(srcFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("metadata payload");
    file.close();

    QVERIFY(QFile::setPermissions(srcFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ReadGroup));

    const struct timespec times[2] = {{1600000000, 123000000}, {1600000000, 456000000}};
    QVERIFY(::utimensat(AT_FDCWD, QFile::encodeName(srcFile).constData(), times, 0) == 0);

    const QByteArray xattrName = QByteArrayLiteral("user.finder.test");
    const QByteArray xattrValue = QByteArrayLiteral("metadata-value");
    const QByteArray srcBytes = QFile::encodeName(srcFile);
    const bool xattrSet =
        ::setxattr(srcBytes.constData(), xattrName.constData(), xattrValue.constData(),
                   static_cast<size_t>(xattrValue.size()), 0) == 0;

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({srcFile}, dstDir.path(), ConflictResolution::Replace));
    QVERIFY(spy.wait(5000));

    const QString dstFile = dstDir.filePath(QStringLiteral("metadata.txt"));
    QVERIFY(QFile::exists(dstFile));

    struct stat srcStat{};
    struct stat dstStat{};
    QVERIFY(::stat(QFile::encodeName(srcFile).constData(), &srcStat) == 0);
    QVERIFY(::stat(QFile::encodeName(dstFile).constData(), &dstStat) == 0);
    QCOMPARE(dstStat.st_mode & 07777, srcStat.st_mode & 07777);
    QCOMPARE(dstStat.st_mtim.tv_sec, srcStat.st_mtim.tv_sec);
    QCOMPARE(dstStat.st_mtim.tv_nsec, srcStat.st_mtim.tv_nsec);

    if (xattrSet) {
        char buffer[128] = {};
        const ssize_t length = ::getxattr(QFile::encodeName(dstFile).constData(),
                                          xattrName.constData(), buffer, sizeof(buffer));
        QVERIFY(length >= 0);
        QCOMPARE(QByteArray(buffer, static_cast<int>(length)), xattrValue);
    }
}

void TestOperations::testOperationsAreQueued() {
    QTemporaryDir firstSrc;
    QTemporaryDir secondSrc;
    QTemporaryDir dstDir;
    QVERIFY(firstSrc.isValid() && secondSrc.isValid() && dstDir.isValid());

    const QString firstFile = firstSrc.filePath(QStringLiteral("first.txt"));
    writeFile(firstFile, "one");
    const QString secondFile = secondSrc.filePath(QStringLiteral("second.txt"));
    writeFile(secondFile, "two");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QSignalSpy queueSpy(&engine, &FileOperationEngine::queueStatusChanged);

    // The second request arrives while the first is still running and must be
    // queued, not dropped.
    QVERIFY(engine.copyItems({firstFile}, dstDir.path(), ConflictResolution::Replace));
    QVERIFY(engine.copyItems({secondFile}, dstDir.path(), ConflictResolution::Replace));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 10000);
    QVERIFY(queueSpy.count() >= 2);
    QCOMPARE(engine.activeAndPendingJobCount(), 0);

    QVERIFY(QFile::exists(dstDir.path() + QStringLiteral("/first.txt")));
    QVERIFY(QFile::exists(dstDir.path() + QStringLiteral("/second.txt")));
}

void TestOperations::testCopyFolderMerge() {
    QTemporaryDir srcRoot;
    QTemporaryDir dstRoot;
    QVERIFY(srcRoot.isValid() && dstRoot.isValid());

    const QString source = srcRoot.filePath(QStringLiteral("project"));
    const QString destination = dstRoot.filePath(QStringLiteral("project"));
    QVERIFY(QDir().mkpath(source + QStringLiteral("/sub")));
    QVERIFY(QDir().mkpath(destination));
    writeFile(source + QStringLiteral("/new.txt"), "new");
    writeFile(source + QStringLiteral("/sub/nested.txt"), "nested");
    writeFile(destination + QStringLiteral("/keep.txt"), "keep");
    writeFile(destination + QStringLiteral("/new.txt"), "old");

    FileOperationEngine engine;
    int folderConflicts = 0;
    int fileConflicts = 0;
    connect(
        &engine, &FileOperationEngine::conflictEncountered, &engine,
        [&](const OperationConflict& conflict, ConflictResolution& resolution, bool& applyToAll) {
            if (conflict.isFolderConflict()) {
                ++folderConflicts;
                resolution = ConflictResolution::Merge;
                applyToAll = true;
            } else {
                ++fileConflicts;
                resolution = ConflictResolution::Replace;
                applyToAll = true;
            }
        });

    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({source}, dstRoot.path(), ConflictResolution::Ask));
    const OperationResult result = waitForOperation(engine, spy);

    QVERIFY(result.success);
    QCOMPARE(folderConflicts, 1);
    QCOMPARE(fileConflicts, 1);
    QCOMPARE(readFile(destination + QStringLiteral("/keep.txt")), QByteArray("keep"));
    QCOMPARE(readFile(destination + QStringLiteral("/new.txt")), QByteArray("new"));
    QCOMPARE(readFile(destination + QStringLiteral("/sub/nested.txt")), QByteArray("nested"));
    QVERIFY(QDir(source).exists());
}

void TestOperations::testMoveFolderMerge() {
    QTemporaryDir srcRoot;
    QTemporaryDir dstRoot;
    QVERIFY(srcRoot.isValid() && dstRoot.isValid());

    const QString source = srcRoot.filePath(QStringLiteral("project"));
    const QString destination = dstRoot.filePath(QStringLiteral("project"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destination));
    writeFile(source + QStringLiteral("/new.txt"), "new");
    writeFile(destination + QStringLiteral("/keep.txt"), "keep");

    FileOperationEngine engine;
    int folderConflicts = 0;
    connect(
        &engine, &FileOperationEngine::conflictEncountered, &engine,
        [&](const OperationConflict& conflict, ConflictResolution& resolution, bool& applyToAll) {
            if (conflict.isFolderConflict()) {
                ++folderConflicts;
                resolution = ConflictResolution::Merge;
                applyToAll = true;
            }
        });

    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.moveItems({source}, dstRoot.path(), ConflictResolution::Ask));
    const OperationResult result = waitForOperation(engine, spy);

    QVERIFY(result.success);
    QCOMPARE(folderConflicts, 1);
    QCOMPARE(readFile(destination + QStringLiteral("/keep.txt")), QByteArray("keep"));
    QCOMPARE(readFile(destination + QStringLiteral("/new.txt")), QByteArray("new"));
    QVERIFY(!QDir(source).exists());
}

void TestOperations::testPauseAndResumeState() {
    FileOperationEngine engine;
    QVERIFY(!engine.isPaused());
    engine.pause();
    QVERIFY(engine.isPaused());
    engine.resume();
    QVERIFY(!engine.isPaused());
}

void TestOperations::testOperationItemStatuses() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());
    const QString first = srcDir.filePath(QStringLiteral("one.txt"));
    const QString second = srcDir.filePath(QStringLiteral("two.txt"));
    for (const QString& path : {first, second}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("payload");
        f.close();
    }

    FileOperationEngine engine;
    QSignalSpy resetSpy(&engine, &FileOperationEngine::operationItemsReset);
    QSignalSpy changeSpy(&engine, &FileOperationEngine::operationItemChanged);
    QSignalSpy finishSpy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({first, second}, dstDir.path(), ConflictResolution::Replace));
    QVERIFY(finishSpy.wait(5000));

    QCOMPARE(resetSpy.count(), 1);
    const QList<OperationItem> items = resetSpy.at(0).at(0).value<QList<OperationItem>>();
    QCOMPARE(items.size(), 2);
    for (const OperationItem& item : items) {
        bool sawDone = false;
        for (const auto& args : changeSpy) {
            if (args.at(0).toULongLong() == item.id &&
                args.at(1).toInt() == static_cast<int>(OperationItemStatus::Done)) {
                sawDone = true;
                break;
            }
        }
        QVERIFY(sawDone);
    }
}

void TestOperations::testCancelPendingItem() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());
    const QString first = srcDir.filePath(QStringLiteral("first.txt"));
    const QString second = srcDir.filePath(QStringLiteral("second.txt"));
    for (const QString& path : {first, second}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("payload");
        f.close();
    }
    QFile existing(dstDir.filePath(QStringLiteral("first.txt")));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("old");
    existing.close();

    FileOperationEngine engine;
    QSignalSpy resetSpy(&engine, &FileOperationEngine::operationItemsReset);
    QSignalSpy finishSpy(&engine, &FileOperationEngine::operationFinished);
    bool cancelledSecond = false;
    connect(&engine, &FileOperationEngine::conflictEncountered, &engine,
            [&](const OperationConflict&, ConflictResolution& resolution, bool& applyToAll) {
                if (!cancelledSecond && resetSpy.count() > 0) {
                    const auto items = resetSpy.at(0).at(0).value<QList<OperationItem>>();
                    for (const OperationItem& item : items) {
                        if (item.sourcePath.endsWith(QStringLiteral("second.txt"))) {
                            engine.cancelItem(item.id);
                            cancelledSecond = true;
                            break;
                        }
                    }
                }
                resolution = ConflictResolution::Skip;
                applyToAll = false;
            });

    QVERIFY(engine.copyItems({first, second}, dstDir.path(), ConflictResolution::Ask));
    QVERIFY(finishSpy.wait(5000));
    const OperationResult result = finishSpy.at(0).at(0).value<OperationResult>();
    QVERIFY(cancelledSecond);
    QVERIFY(!QFile::exists(dstDir.filePath(QStringLiteral("second.txt"))));
    QCOMPARE(result.cancelledCount, 1);
}

QTEST_MAIN(TestOperations)
#include "test_operations.moc"
