#include "RecentFilesManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class TestRecentFiles : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void testAddAndList();
    void testPreservesExistingApplications();
    void testClear();
    void testTrailingSlashFolderHrefs();

private:
    static void writeFile(const QString& path, const QByteArray& contents);
};

void TestRecentFiles::cleanup() {
    RecentFilesManager::setStoragePathForTesting(QString());
}

void TestRecentFiles::writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(contents);
    file.close();
}

void TestRecentFiles::testAddAndList() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storage = directory.path() + QStringLiteral("/recently-used.xbel");
    RecentFilesManager::setStoragePathForTesting(storage);

    const QString firstPath = directory.path() + QStringLiteral("/a.txt");
    const QString secondPath = directory.path() + QStringLiteral("/b.png");
    writeFile(firstPath, "a");
    writeFile(secondPath, "b");

    QVERIFY(RecentFilesManager::add(firstPath, QStringLiteral("text/plain")));
    QVERIFY(RecentFilesManager::add(secondPath, QStringLiteral("image/png")));

    const QList<RecentFile> recent = RecentFilesManager::list(10);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.at(0).path, secondPath);
    QCOMPARE(recent.at(1).path, firstPath);
    QCOMPARE(recent.at(0).mimeType, QStringLiteral("image/png"));
}

void TestRecentFiles::testPreservesExistingApplications() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storage = directory.path() + QStringLiteral("/recently-used.xbel");
    RecentFilesManager::setStoragePathForTesting(storage);

    const QString existingPath = directory.path() + QStringLiteral("/existing.txt");
    const QString newPath = directory.path() + QStringLiteral("/new.txt");
    writeFile(existingPath, "existing");
    writeFile(newPath, "new");

    const QString existingHref = QUrl::fromLocalFile(existingPath).toString(QUrl::FullyEncoded);
    const QByteArray existingXbel =
        QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<xbel version=\"1.0\" "
            "xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\" "
            "xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
            "  <bookmark href=\"%1\" added=\"2026-01-01T00:00:00Z\" "
            "modified=\"2026-01-01T00:00:00Z\" visited=\"2026-01-01T00:00:00Z\">\n"
            "    <info><metadata owner=\"http://freedesktop.org\">\n"
            "      <mime:mime-type type=\"text/plain\"/>\n"
            "      <bookmark:groups><bookmark:group>Documents</bookmark:group></bookmark:groups>\n"
            "      <bookmark:applications>\n"
            "        <bookmark:application name=\"OtherApp\" exec=\"otherapp %u\" "
            "modified=\"2026-01-01T00:00:00Z\" count=\"3\"/>\n"
            "      </bookmark:applications>\n"
            "    </metadata></info>\n"
            "  </bookmark>\n"
            "</xbel>\n")
            .arg(existingHref)
            .toUtf8();
    writeFile(storage, existingXbel);

    QVERIFY(RecentFilesManager::add(newPath, QStringLiteral("text/plain")));

    QFile file(storage);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString xml = QString::fromUtf8(file.readAll());
    QVERIFY(xml.contains(QStringLiteral("OtherApp")));
    QVERIFY(xml.contains(QStringLiteral("Documents")));
    QVERIFY(xml.contains(QStringLiteral("bookmark:groups")));
    QVERIFY(xml.contains(QStringLiteral("name=\"File Manager\"")));
    QVERIFY(xml.contains(existingHref));
    QVERIFY(xml.contains(QUrl::fromLocalFile(newPath).toString(QUrl::FullyEncoded)));

    const QList<RecentFile> recent = RecentFilesManager::list(10);
    QCOMPARE(recent.size(), 2);
}

void TestRecentFiles::testClear() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storage = directory.path() + QStringLiteral("/recently-used.xbel");
    RecentFilesManager::setStoragePathForTesting(storage);

    const QString path = directory.path() + QStringLiteral("/file.txt");
    writeFile(path, "file");
    QVERIFY(RecentFilesManager::add(path, QStringLiteral("text/plain")));
    QVERIFY(QFile::exists(storage));

    RecentFilesManager::clear();
    QVERIFY(!QFile::exists(storage));
    QVERIFY(RecentFilesManager::list(10).isEmpty());
}

void TestRecentFiles::testTrailingSlashFolderHrefs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString storage = directory.path() + QStringLiteral("/recently-used.xbel");
    RecentFilesManager::setStoragePathForTesting(storage);

    const QString folderPath = directory.path() + QStringLiteral("/Vulnerability Report");
    QVERIFY(QDir().mkpath(folderPath));

    // Folder hrefs written with a trailing slash must still yield a named entry.
    const QString href =
        QUrl::fromLocalFile(folderPath + QStringLiteral("/")).toString(QUrl::FullyEncoded);
    writeFile(
        storage,
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<xbel version=\"1.0\" "
                       "xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\" "
                       "xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                       "  <bookmark href=\"%1\" added=\"2026-01-01T00:00:00Z\" "
                       "modified=\"2026-01-01T00:00:00Z\" visited=\"2026-01-01T00:00:00Z\">\n"
                       "    <info><metadata owner=\"http://freedesktop.org\">\n"
                       "      <mime:mime-type type=\"inode/directory\"/>\n"
                       "    </metadata></info>\n"
                       "  </bookmark>\n"
                       "</xbel>\n")
            .arg(href)
            .toUtf8());

    const QList<RecentFile> recent = RecentFilesManager::list(10);
    QCOMPARE(recent.size(), 1);
    QCOMPARE(recent.at(0).path, folderPath);
    QCOMPARE(QFileInfo(recent.at(0).path).fileName(), QStringLiteral("Vulnerability Report"));
}

QTEST_MAIN(TestRecentFiles)
#include "test_recentfiles.moc"
