#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "ArchiveService.h"
#include "FileOperationEngine.h"

class TestArchives : public QObject {
    Q_OBJECT

private slots:
    void testIsArchive();
    void testNamingHelpers();
    void testZipCompressAndExtract();
    void testTarGzCompressAndExtract();
    void testAsyncEngineCompressAndExtract();
};

void TestArchives::testIsArchive()
{
    QVERIFY(ArchiveService::isArchive(QStringLiteral("/path/to/archive.zip")));
    QVERIFY(ArchiveService::isArchive(QStringLiteral("/path/to/archive.tar.gz")));
    QVERIFY(ArchiveService::isArchive(QStringLiteral("/path/to/archive.tar.xz")));
    QVERIFY(ArchiveService::isArchive(QStringLiteral("/path/to/archive.7z")));
    QVERIFY(ArchiveService::isArchive(QStringLiteral("/path/to/archive.tgz")));
    QVERIFY(!ArchiveService::isArchive(QStringLiteral("/path/to/document.pdf")));
    QVERIFY(!ArchiveService::isArchive(QStringLiteral("/path/to/plain.txt")));
    QVERIFY(!ArchiveService::isArchive(QStringLiteral("/path/to/folder")));
}

void TestArchives::testNamingHelpers()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString file1 = temp.filePath(QStringLiteral("my_doc.txt"));
    QFile f(file1);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();

    QString singleSuggested = ArchiveService::suggestedArchiveName({file1}, QStringLiteral("zip"));
    QCOMPARE(QFileInfo(singleSuggested).fileName(), QStringLiteral("my_doc.txt.zip"));

    QString multiSuggested = ArchiveService::suggestedArchiveName({file1, temp.path()}, QStringLiteral("tar.gz"));
    QCOMPARE(QFileInfo(multiSuggested).fileName(), QStringLiteral("Archive.tar.gz"));

    QString extractDir = ArchiveService::suggestedExtractDir(temp.filePath(QStringLiteral("Photos.zip")));
    QCOMPARE(QFileInfo(extractDir).fileName(), QStringLiteral("Photos"));
}

void TestArchives::testZipCompressAndExtract()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // Create a folder with subfiles
    QString subDirPath = temp.filePath(QStringLiteral("folder_to_zip"));
    QDir().mkpath(subDirPath);

    QString fileA = subDirPath + QStringLiteral("/a.txt");
    QFile fa(fileA);
    QVERIFY(fa.open(QIODevice::WriteOnly));
    fa.write("Sample content for file A");
    fa.close();

    QString fileB = subDirPath + QStringLiteral("/b.txt");
    QFile fb(fileB);
    QVERIFY(fb.open(QIODevice::WriteOnly));
    fb.write("Another content for file B");
    fb.close();

    QString zipPath = temp.filePath(QStringLiteral("output.zip"));

    QString error;
    bool compOk = ArchiveService::compress({subDirPath}, zipPath, nullptr, nullptr, &error);
    QVERIFY2(compOk, qPrintable(error));
    QVERIFY(QFile::exists(zipPath));
    QVERIFY(QFileInfo(zipPath).size() > 0);

    // Extract
    QString extractDest = temp.filePath(QStringLiteral("extracted_folder"));
    bool extOk = ArchiveService::extract(zipPath, extractDest, nullptr, nullptr, &error);
    QVERIFY2(extOk, qPrintable(error));

    QVERIFY(QFile::exists(extractDest + QStringLiteral("/folder_to_zip/a.txt")));
    QVERIFY(QFile::exists(extractDest + QStringLiteral("/folder_to_zip/b.txt")));

    QFile readA(extractDest + QStringLiteral("/folder_to_zip/a.txt"));
    QVERIFY(readA.open(QIODevice::ReadOnly));
    QCOMPARE(readA.readAll(), QByteArray("Sample content for file A"));
}

void TestArchives::testTarGzCompressAndExtract()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString fileA = temp.filePath(QStringLiteral("tar_test.txt"));
    QFile fa(fileA);
    QVERIFY(fa.open(QIODevice::WriteOnly));
    fa.write("Testing tar gzip compression");
    fa.close();

    QString tarPath = temp.filePath(QStringLiteral("output.tar.gz"));
    QString error;
    bool compOk = ArchiveService::compress({fileA}, tarPath, nullptr, nullptr, &error);
    QVERIFY2(compOk, qPrintable(error));
    QVERIFY(QFile::exists(tarPath));

    QString extractDest = temp.filePath(QStringLiteral("extracted_tar"));
    bool extOk = ArchiveService::extract(tarPath, extractDest, nullptr, nullptr, &error);
    QVERIFY2(extOk, qPrintable(error));

    QString extractedFile = extractDest + QStringLiteral("/tar_test.txt");
    QVERIFY(QFile::exists(extractedFile));
    QFile readBack(extractedFile);
    QVERIFY(readBack.open(QIODevice::ReadOnly));
    QCOMPARE(readBack.readAll(), QByteArray("Testing tar gzip compression"));
}

void TestArchives::testAsyncEngineCompressAndExtract()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString fileA = temp.filePath(QStringLiteral("data.bin"));
    QFile fa(fileA);
    QVERIFY(fa.open(QIODevice::WriteOnly));
    fa.write(QByteArray(1024 * 64, 'X'));
    fa.close();

    FileOperationEngine engine;
    QSignalSpy spyStarted(&engine, &FileOperationEngine::operationStarted);
    QSignalSpy spyFinished(&engine, &FileOperationEngine::operationFinished);

    QString zipPath = temp.filePath(QStringLiteral("async_test.zip"));
    engine.compressItems({fileA}, zipPath);

    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.count(), 1);
    OperationResult compRes = qvariant_cast<OperationResult>(spyFinished.takeFirst().at(0));
    QVERIFY(compRes.success);
    QVERIFY(QFile::exists(zipPath));

    // Async extract
    QString extractDest = temp.filePath(QStringLiteral("async_extracted"));
    engine.extractArchive(zipPath, extractDest);
    QVERIFY(spyFinished.wait(5000));
    QCOMPARE(spyFinished.count(), 1);
    OperationResult extRes = qvariant_cast<OperationResult>(spyFinished.takeFirst().at(0));
    QVERIFY(extRes.success);
    QVERIFY(QFile::exists(extractDest + QStringLiteral("/data.bin")));
}

QTEST_MAIN(TestArchives)
#include "test_archives.moc"
