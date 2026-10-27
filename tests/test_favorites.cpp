#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardPaths>
#include "AppSettings.h"

class TestFavorites : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testDefaultFavorites();
    void testAddAndOrder();
    void testMoveToTop();
    void testMoveUpAndDown();
    void testReorder();
    void testRemoveFavorite();
};

void TestFavorites::init()
{
    AppSettings::instance().resetSidebarDefaults();
}

void TestFavorites::cleanup()
{
    AppSettings::instance().resetSidebarDefaults();
}

void TestFavorites::testDefaultFavorites()
{
    QStringList order = AppSettings::instance().favoriteOrder();
    QVERIFY(!order.isEmpty());
    QVERIFY(order.contains(QDir::homePath()));
    QCOMPARE(order.first(), QDir::homePath());
}

void TestFavorites::testAddAndOrder()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString testPath = tempDir.path();

    AppSettings::instance().addFavorite(testPath);
    QVERIFY(AppSettings::instance().customFavorites().contains(testPath));

    QStringList order = AppSettings::instance().favoriteOrder();
    QVERIFY(order.contains(testPath));

    // Custom favorite should be at the end initially
    QCOMPARE(order.last(), testPath);
}

void TestFavorites::testMoveToTop()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString testPath = tempDir.path();

    AppSettings::instance().addFavorite(testPath);
    AppSettings::instance().moveFavoriteToTop(testPath);

    QStringList order = AppSettings::instance().favoriteOrder();
    QCOMPARE(order.first(), testPath);

    // Also test standard favorite to top
    QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!downloads.isEmpty() && order.contains(downloads)) {
        AppSettings::instance().moveFavoriteToTop(downloads);
        QStringList order2 = AppSettings::instance().favoriteOrder();
        QCOMPARE(order2.first(), downloads);
    }
}

void TestFavorites::testMoveUpAndDown()
{
    QStringList initial = AppSettings::instance().favoriteOrder();
    QVERIFY(initial.size() >= 3);

    QString secondItem = initial.at(1);
    AppSettings::instance().moveFavoriteUp(secondItem);
    QStringList afterUp = AppSettings::instance().favoriteOrder();
    QCOMPARE(afterUp.at(0), secondItem);

    AppSettings::instance().moveFavoriteDown(secondItem);
    QStringList afterDown = AppSettings::instance().favoriteOrder();
    QCOMPARE(afterDown.at(1), secondItem);
}

void TestFavorites::testReorder()
{
    QStringList initial = AppSettings::instance().favoriteOrder();
    QVERIFY(initial.size() >= 3);

    QString firstItem = initial.at(0);
    // Move to index 2
    AppSettings::instance().reorderFavorite(firstItem, 2);
    QStringList afterReorder = AppSettings::instance().favoriteOrder();
    QCOMPARE(afterReorder.at(2), firstItem);
}

void TestFavorites::testRemoveFavorite()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    QString testPath = tempDir.path();

    AppSettings::instance().addFavorite(testPath);
    QVERIFY(AppSettings::instance().favoriteOrder().contains(testPath));

    AppSettings::instance().removeFavorite(testPath);
    QVERIFY(!AppSettings::instance().customFavorites().contains(testPath));
    QVERIFY(!AppSettings::instance().favoriteOrder().contains(testPath));
}

QTEST_MAIN(TestFavorites)
#include "test_favorites.moc"
