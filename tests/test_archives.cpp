#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <sys/stat.h>
#include "ArchiveService.h"
#include "FileOperationEngine.h"
#include <archive.h>
#include <archive_entry.h>

class TestArchives : public QObject {
    Q_OBJECT

private slots:
    void testIsArchive();
    void testNamingHelpers();
    void testZipCompressAndExtract();
    void testTarGzCompressAndExtract();
    void testZipPasswordEncryption();
    void testExtractAppImage();
    void testAsyncEngineCompressAndExtract();
    void testPathTraversalExtractionSecurity();
    void testExtractDoesNotOverwriteExisting();
    void testExtractCorruptArchiveFails();
    void testCompressFailureKeepsExistingArchive();
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

void TestArchives::testZipPasswordEncryption()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString source = temp.filePath(QStringLiteral("secret.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("top secret");
    file.close();

    const QString archivePath = temp.filePath(QStringLiteral("secret.zip"));
    QString error;
    QVERIFY2(ArchiveService::compress({source}, archivePath, nullptr, nullptr,
                                     &error, 9, QStringLiteral("hunter2")),
             qPrintable(error));
    QVERIFY(QFile::exists(archivePath));

    // Encrypted data must not extract without the passphrase.
    QString noPasswordError;
    QVERIFY(!ArchiveService::extract(archivePath,
                                     temp.filePath(QStringLiteral("no_password")),
                                     nullptr, nullptr, &noPasswordError));
    QVERIFY(!noPasswordError.isEmpty());

    const QString destination = temp.filePath(QStringLiteral("with_password"));
    QString extractError;
    QVERIFY2(ArchiveService::extract(archivePath, destination, nullptr, nullptr,
                                     &extractError, nullptr, QStringLiteral("hunter2")),
             qPrintable(extractError));

    QFile extracted(destination + QStringLiteral("/secret.txt"));
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArray("top secret"));
}

void TestArchives::testExtractAppImage()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString fakeAppImage = temp.filePath(QStringLiteral("sample.AppImage"));
    QFile script(fakeAppImage);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write(
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--appimage-extract\" ]; then\n"
        "  mkdir squashfs-root\n"
        "  printf extracted > squashfs-root/content.txt\n"
        "  exit 0\n"
        "fi\n"
        "exit 1\n");
    script.close();
    QVERIFY(QFile::setPermissions(fakeAppImage,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    if (!QFileInfo(fakeAppImage).isExecutable()) {
        QSKIP("Temporary filesystem is mounted noexec");
    }

    QString outputDirectory;
    QString error;
    QVERIFY2(ArchiveService::extractAppImage(fakeAppImage, &outputDirectory, &error),
             qPrintable(error));
    QCOMPARE(outputDirectory, temp.filePath(QStringLiteral("sample-extracted")));

    QFile content(outputDirectory + QStringLiteral("/content.txt"));
    QVERIFY(content.open(QIODevice::ReadOnly));
    QCOMPARE(content.readAll(), QByteArray("extracted"));
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

void TestArchives::testPathTraversalExtractionSecurity()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QString maliciousArchive = temp.filePath(QStringLiteral("malicious.tar"));
    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, maliciousArchive.toLocal8Bit().constData());

    struct archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, "../../outside_file.txt");
    archive_entry_set_size(entry, 4);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    archive_write_data(a, "leak", 4);
    archive_entry_free(entry);

    archive_write_close(a);
    archive_write_free(a);

    QString extractDest = temp.filePath(QStringLiteral("extracted"));
    QDir().mkpath(extractDest);

    QString errorMsg;
    ArchiveService::extract(maliciousArchive, extractDest, nullptr, nullptr, &errorMsg);

    // Verify that the file NEVER escaped to parent directories and was safely rejected/skipped
    QVERIFY(!QFile::exists(temp.filePath(QStringLiteral("outside_file.txt"))));
    QVERIFY(!QFile::exists(temp.path() + QStringLiteral("/../outside_file.txt")));
}

void TestArchives::testExtractDoesNotOverwriteExisting()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString srcFile = temp.filePath(QStringLiteral("note.txt"));
    QFile src(srcFile);
    QVERIFY(src.open(QIODevice::WriteOnly));
    src.write("new content");
    src.close();

    const QString zipPath = temp.filePath(QStringLiteral("data.zip"));
    QString error;
    QVERIFY2(ArchiveService::compress({srcFile}, zipPath, nullptr, nullptr, &error), qPrintable(error));

    const QString dest = temp.filePath(QStringLiteral("dest"));
    QVERIFY(QDir().mkpath(dest));
    const QString existing = dest + QStringLiteral("/note.txt");
    QFile old(existing);
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write("old content");
    old.close();

    QString warning;
    QVERIFY2(ArchiveService::extract(zipPath, dest, nullptr, nullptr, &error, &warning), qPrintable(error));
    QVERIFY2(!warning.isEmpty(), "existing files must be reported as skipped");

    QFile check(existing);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("old content"));
}

void TestArchives::testExtractCorruptArchiveFails()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString badArchive = temp.filePath(QStringLiteral("broken.zip"));
    QFile bad(badArchive);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("this is definitely not a zip archive");
    bad.close();

    QString error;
    const bool ok = ArchiveService::extract(badArchive, temp.filePath(QStringLiteral("out")),
                                            nullptr, nullptr, &error);
    QVERIFY2(!ok, "a corrupt archive must not be reported as extracted");
    QVERIFY(!error.isEmpty());
}

void TestArchives::testCompressFailureKeepsExistingArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString archivePath = temp.filePath(QStringLiteral("existing.zip"));
    QFile existing(archivePath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("ORIGINAL ARCHIVE DATA");
    existing.close();

    const QString tree = temp.filePath(QStringLiteral("tree"));
    QVERIFY(QDir().mkpath(tree));
    QFile regular(tree + QStringLiteral("/file.txt"));
    QVERIFY(regular.open(QIODevice::WriteOnly));
    regular.write("data");
    regular.close();
    QVERIFY(::mkfifo(QFile::encodeName(tree + QStringLiteral("/pipe")).constData(), 0644) == 0);

    QString error;
    QVERIFY(!ArchiveService::compress({tree}, archivePath, nullptr, nullptr, &error));
    QVERIFY(!error.isEmpty());

    QFile check(archivePath);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), QByteArray("ORIGINAL ARCHIVE DATA"));

    const auto leftovers = QDir(temp.path()).entryList({QStringLiteral("existing.zip.part-*")}, QDir::Files | QDir::Hidden);
    QVERIFY(leftovers.isEmpty());
}

QTEST_MAIN(TestArchives)
#include "test_archives.moc"
