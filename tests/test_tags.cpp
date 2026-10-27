#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "TagManager.h"
#include "FileItem.h"

class TestTags : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testStandardTags();
    void testAddRemoveToggleTags();
    void testFileItemTags();
    void testTagQueries();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
};

void TestTags::initTestCase()
{
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());
}

void TestTags::cleanupTestCase()
{
    m_tempDir.reset();
}

void TestTags::testStandardTags()
{
    const auto& tags = TagManager::standardTags();
    QCOMPARE(tags.size(), 7);
    QCOMPARE(tags.at(0).name, QStringLiteral("Red"));
    QCOMPARE(tags.at(1).name, QStringLiteral("Orange"));
    QCOMPARE(tags.at(2).name, QStringLiteral("Yellow"));
    QCOMPARE(tags.at(3).name, QStringLiteral("Green"));
    QCOMPARE(tags.at(4).name, QStringLiteral("Blue"));
    QCOMPARE(tags.at(5).name, QStringLiteral("Purple"));
    QCOMPARE(tags.at(6).name, QStringLiteral("Gray"));

    QVERIFY(TagManager::colorForTag(QStringLiteral("Red")).isValid());
    QVERIFY(!TagManager::tagDotIcon(QStringLiteral("Blue")).isNull());
}

void TestTags::testAddRemoveToggleTags()
{
    QString filePath = m_tempDir->filePath("tagged_doc.txt");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("sample content");
    file.close();

    auto& tm = TagManager::instance();

    // Initial state: no tags
    QVERIFY(tm.tagsForFile(filePath).isEmpty());
    QVERIFY(!tm.hasTag(filePath, "Red"));

    // Add Red
    QVERIFY(tm.addTag(filePath, "Red"));
    QVERIFY(tm.hasTag(filePath, "Red"));
    QCOMPARE(tm.tagsForFile(filePath).size(), 1);

    // Toggle Blue (adds Blue)
    QVERIFY(tm.toggleTag(filePath, "Blue"));
    QVERIFY(tm.hasTag(filePath, "Blue"));
    QCOMPARE(tm.tagsForFile(filePath).size(), 2);

    // Toggle Red (removes Red)
    QVERIFY(tm.toggleTag(filePath, "Red"));
    QVERIFY(!tm.hasTag(filePath, "Red"));
    QVERIFY(tm.hasTag(filePath, "Blue"));

    // Clear all tags
    QVERIFY(tm.clearTags(filePath));
    QVERIFY(tm.tagsForFile(filePath).isEmpty());
}

void TestTags::testFileItemTags()
{
    QString filePath = m_tempDir->filePath("file_item_test.txt");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("hello tags");
    file.close();

    TagManager::instance().setTagsForFile(filePath, {"Green", "Purple"});

    FileItem item(filePath);
    QCOMPARE(item.tags().size(), 2);
    QVERIFY(item.tags().contains("Green"));
    QVERIFY(item.tags().contains("Purple"));
}

void TestTags::testTagQueries()
{
    QString f1 = m_tempDir->filePath("query_1.txt");
    QString f2 = m_tempDir->filePath("query_2.txt");
    QFile file1(f1);
    QVERIFY(file1.open(QIODevice::WriteOnly));
    file1.close();
    QFile file2(f2);
    QVERIFY(file2.open(QIODevice::WriteOnly));
    file2.close();

    TagManager::instance().addTag(f1, "Yellow");
    TagManager::instance().addTag(f2, "Yellow");

    QStringList yellowFiles = TagManager::instance().filesForTag("Yellow");
    QVERIFY(yellowFiles.contains(f1));
    QVERIFY(yellowFiles.contains(f2));
    QVERIFY(TagManager::instance().tagCount("Yellow") >= 2);
}

QTEST_MAIN(TestTags)
#include "test_tags.moc"
