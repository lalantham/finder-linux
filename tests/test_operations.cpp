#include "FileOperationEngine.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
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
    void testCopyPreservesSparseFiles();
    void testCopyHandlesLargeFile();
    void testCopyFreeSpacePreflight();
    void testMultiFileProgressIsCumulative();
    void testSparseFileNotBlockedBySpacePreflight();
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

void TestOperations::testCopyPreservesSparseFiles() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    // 64 MiB logical file with only 8 KiB of real data: a hole in the middle.
    const QString src = srcDir.filePath(QStringLiteral("sparse.bin"));
    constexpr off_t logicalSize = 64LL * 1024 * 1024;
    {
        const QByteArray head(4096, 'A');
        const QByteArray tail(4096, 'B');
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write(head) == head.size());
        QVERIFY(f.seek(logicalSize - tail.size()));
        QVERIFY(f.write(tail) == tail.size());
        f.close();
    }

    struct stat srcStat{};
    QCOMPARE(::stat(QFile::encodeName(src).constData(), &srcStat), 0);
    QVERIFY2(srcStat.st_size == logicalSize, "test fixture should be sparse");
    const qint64 srcAllocated = static_cast<qint64>(srcStat.st_blocks) * 512;
    QVERIFY2(srcAllocated < 1024 * 1024, "test fixture should have a hole, not full allocation");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({src}, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);
    QVERIFY2(res.success, qPrintable(res.errorMessage));

    const QString dst = dstDir.filePath(QStringLiteral("sparse.bin"));
    struct stat dstStat{};
    QCOMPARE(::stat(QFile::encodeName(dst).constData(), &dstStat), 0);

    // Logical size must match exactly...
    QCOMPARE(static_cast<qint64>(dstStat.st_size), static_cast<qint64>(logicalSize));

    // ...and the hole must still be a hole. A buffered read/write loop would
    // materialize the full 64 MiB here, wasting disk and I/O.
    const qint64 dstAllocated = static_cast<qint64>(dstStat.st_blocks) * 512;
    QVERIFY2(dstAllocated < 8 * 1024 * 1024,
             qPrintable(QStringLiteral("copy materialized sparse file: %1 bytes allocated "
                                       "for a %2 byte hole")
                            .arg(dstAllocated)
                            .arg(static_cast<qint64>(logicalSize))));

    // The data at both ends must survive.
    QFile out(dst);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.read(4096), QByteArray(4096, 'A'));
    QVERIFY(out.seek(logicalSize - 4096));
    QCOMPARE(out.read(4096), QByteArray(4096, 'B'));
    out.close();

    // The hole itself must read back as zeros.
    QVERIFY(out.open(QIODevice::ReadOnly));
    QVERIFY(out.seek(1024 * 1024));
    const QByteArray middle = out.read(4096);
    QCOMPARE(middle.size(), 4096);
    QVERIFY(std::all_of(middle.cbegin(), middle.cend(), [](char c) { return c == '\0'; }));
}

void TestOperations::testCopyHandlesLargeFile() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    // Larger than one 1 MB chunk so the loop iterates several times.
    const QString src = srcDir.filePath(QStringLiteral("large.bin"));
    QByteArray content;
    content.reserve(6 * 1024 * 1024);
    for (int i = 0; i < 6 * 1024 * 1024; ++i) {
        content.append(static_cast<char>(i % 251));
    }
    writeFile(src, content);

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({src}, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);
    QVERIFY2(res.success, qPrintable(res.errorMessage));

    const QString dst = dstDir.filePath(QStringLiteral("large.bin"));
    QCOMPARE(readFile(dst), content);

    // Progress must still be reported for kernel-side copies, otherwise the
    // speed and ETA in the progress dialog would stay at zero.
    QVERIFY(res.processedCount >= 1);
}

void TestOperations::testCopyFreeSpacePreflight() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    const QString src = srcDir.filePath(QStringLiteral("data.bin"));
    writeFile(src, QByteArray(1024, 'x'));

    // The preflight must not reject a copy that fits comfortably.
    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({src}, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);
    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QVERIFY(QFile::exists(dstDir.filePath(QStringLiteral("data.bin"))));

    // And a same-filesystem move must never be blocked by a free-space check,
    // since it is only a rename that consumes no space.
    FileOperationEngine moveEngine;
    QSignalSpy moveSpy(&moveEngine, &FileOperationEngine::operationFinished);
    const QString moved = dstDir.filePath(QStringLiteral("data.bin"));
    QVERIFY(moveEngine.moveItems({moved}, srcDir.path(), ConflictResolution::Replace));
    const OperationResult moveRes = waitForOperation(moveEngine, moveSpy);
    QVERIFY2(moveRes.success, qPrintable(moveRes.errorMessage));
    QVERIFY(QFile::exists(srcDir.filePath(QStringLiteral("data.bin"))));
}

void TestOperations::testMultiFileProgressIsCumulative() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    // Two files so an item-by-item progress reset would be visible.
    QStringList sources;
    for (const QString& name : {QStringLiteral("one.bin"), QStringLiteral("two.bin")}) {
        const QString path = srcDir.filePath(name);
        writeFile(path, QByteArray(300 * 1024, 'x'));
        sources << path;
    }

    FileOperationEngine engine;
    QStringList reported;
    qint64 highestProcessed = 0;
    qint64 declaredTotal = 0;
    connect(&engine, &FileOperationEngine::progress, this, [&](const OperationProgress& progress) {
        highestProcessed = qMax(highestProcessed, progress.bytesProcessed);
        declaredTotal = progress.totalBytes;
        reported << QString::number(progress.bytesProcessed);
    });

    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems(sources, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);
    QVERIFY2(res.success, qPrintable(res.errorMessage));

    QCOMPARE(declaredTotal, static_cast<qint64>(600 * 1024));
    // Progress must accumulate across items; overwriting it per item would
    // leave the bar stuck at the size of the largest single file.
    QVERIFY2(highestProcessed >= declaredTotal,
             qPrintable(QStringLiteral("progress stalled at %1 of %2 bytes")
                            .arg(highestProcessed)
                            .arg(declaredTotal)));
}

void TestOperations::testSparseFileNotBlockedBySpacePreflight() {
    QTemporaryDir srcDir;
    QTemporaryDir dstDir;
    QVERIFY(srcDir.isValid() && dstDir.isValid());

    // A sparse file whose logical size is far larger than the free space of any
    // realistic test filesystem, but which occupies next to nothing. The
    // preflight must measure allocated bytes, not the logical length, or this
    // copy would be rejected even though it needs no room.
    const QString src = srcDir.filePath(QStringLiteral("huge-sparse.img"));
    constexpr off_t logicalSize = 1024LL * 1024 * 1024 * 1024; // 1 TiB logical
    {
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write(QByteArray(4096, 'H')) == 4096);
        QVERIFY(f.seek(logicalSize - 4096));
        QVERIFY(f.write(QByteArray(4096, 'T')) == 4096);
        f.close();
    }

    struct stat st{};
    QCOMPARE(::stat(QFile::encodeName(src).constData(), &st), 0);
    QVERIFY2(st.st_blocks * 512 < 1024 * 1024, "fixture should be almost entirely holes");

    FileOperationEngine engine;
    QSignalSpy spy(&engine, &FileOperationEngine::operationFinished);
    QVERIFY(engine.copyItems({src}, dstDir.path(), ConflictResolution::Replace));
    const OperationResult res = waitForOperation(engine, spy);

    QVERIFY2(res.success, qPrintable(res.errorMessage));
    QVERIFY(!res.errorMessage.contains(QStringLiteral("Not enough free space")));
    QVERIFY(QFile::exists(dstDir.filePath(QStringLiteral("huge-sparse.img"))));
}

QTEST_MAIN(TestOperations)
#include "test_operations.moc"
