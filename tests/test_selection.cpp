#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QApplication>
#include "IconView.h"
#include "ListView.h"
#include "ColumnView.h"
#include "GalleryView.h"
#include "TabPage.h"
#include "FileSystemModel.h"

class TestSelection : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testIconViewSelection();
    void testListViewSelection();
    void testColumnViewSelection();
    void testGalleryViewSelection();
    void testTabPageMultiSelection();
    void testNavigationHistory();

private:
    QTemporaryDir m_tempDir;
    QStringList m_createdFiles;
};

void TestSelection::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    QDir dir(m_tempDir.path());
    for (int i = 1; i <= 5; ++i) {
        QString fileName = QString("file%1.txt").arg(i);
        QFile f(dir.filePath(fileName));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("test");
        f.close();
        m_createdFiles.append(dir.filePath(fileName));
    }
}

void TestSelection::testIconViewSelection()
{
    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    IconView iconView;
    iconView.setFileSystemModel(model);
    iconView.resize(600, 400);
    iconView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&iconView));

    QModelIndex idx0 = model->index(0, 0);
    QModelIndex idx1 = model->index(1, 0);
    QVERIFY(idx0.isValid());
    QVERIFY(idx1.isValid());

    QRect r0 = iconView.visualRect(idx0);
    QRect r1 = iconView.visualRect(idx1);

    // 1. Click first item
    QTest::mouseClick(iconView.viewport(), Qt::LeftButton, Qt::NoModifier, r0.center());
    QCOMPARE(iconView.selectionModel()->selectedIndexes().size(), 1);

    // 2. Ctrl+Click second item -> 2 items selected
    QTest::mouseClick(iconView.viewport(), Qt::LeftButton, Qt::ControlModifier, r1.center());
    QCOMPARE(iconView.selectionModel()->selectedIndexes().size(), 2);

    // 3. Shift+Click range to 5th item -> all 5 items selected
    QModelIndex idx4 = model->index(4, 0);
    QRect r4 = iconView.visualRect(idx4);
    QTest::mouseClick(iconView.viewport(), Qt::LeftButton, Qt::ShiftModifier, r4.center());
    QCOMPARE(iconView.selectionModel()->selectedIndexes().size(), 5);

    // 4. Select all
    iconView.clearSelection();
    QCOMPARE(iconView.selectionModel()->selectedIndexes().size(), 0);
    iconView.selectAll();
    // FileSystemModel has 4 columns per row, so 5 rows * 4 columns = 20 indexes
    QCOMPARE(iconView.selectionModel()->selectedIndexes().size(), 20);
}

void TestSelection::testListViewSelection()
{
    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    ListView listView;
    listView.setFileSystemModel(model);
    listView.resize(600, 400);
    listView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&listView));

    QModelIndex idx0 = model->index(0, 0);
    QModelIndex idx1 = model->index(1, 0);
    QVERIFY(idx0.isValid());
    QVERIFY(idx1.isValid());

    QRect r0 = listView.visualRect(idx0);
    QRect r1 = listView.visualRect(idx1);

    // 1. Click first item
    QTest::mouseClick(listView.viewport(), Qt::LeftButton, Qt::NoModifier, r0.center());
    QCOMPARE(listView.selectionModel()->selectedRows().size(), 1);

    // 2. Ctrl+Click second item -> 2 rows selected
    QTest::mouseClick(listView.viewport(), Qt::LeftButton, Qt::ControlModifier, r1.center());
    QCOMPARE(listView.selectionModel()->selectedRows().size(), 2);

    // 3. Shift+Click range to 5th item -> all 5 items selected
    QModelIndex idx4 = model->index(4, 0);
    QRect r4 = listView.visualRect(idx4);
    QTest::mouseClick(listView.viewport(), Qt::LeftButton, Qt::ShiftModifier, r4.center());
    QCOMPARE(listView.selectionModel()->selectedRows().size(), 5);

    // 4. Select all
    listView.clearSelection();
    QCOMPARE(listView.selectionModel()->selectedRows().size(), 0);
    listView.selectAll();
    QCOMPARE(listView.selectionModel()->selectedRows().size(), 5);
}

void TestSelection::testColumnViewSelection()
{
    ColumnView colView;
    colView.setRootPath(m_tempDir.path());
    colView.resize(600, 400);
    colView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&colView));

    auto* list = colView.findChild<ColumnBrowserList*>();
    QVERIFY(list != nullptr);

    auto* model = list->model();
    QVERIFY(model != nullptr);

    QModelIndex idx0 = model->index(0, 0);
    QModelIndex idx1 = model->index(1, 0);
    QVERIFY(idx0.isValid());
    QVERIFY(idx1.isValid());

    QRect r0 = list->visualRect(idx0);
    QRect r1 = list->visualRect(idx1);

    // 1. Click first item
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, r0.center());
    QCOMPARE(list->selectionModel()->selectedIndexes().size(), 1);
    QCOMPARE(colView.selectedPaths().size(), 1);

    // 2. Ctrl+Click second item -> 2 items selected in ColumnView!
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, r1.center());
    QCOMPARE(list->selectionModel()->selectedIndexes().size(), 2);
    QCOMPARE(colView.selectedPaths().size(), 2);

    // 3. Shift+Click 5th item -> all 5 items selected!
    QModelIndex idx4 = model->index(4, 0);
    QRect r4 = list->visualRect(idx4);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ShiftModifier, r4.center());
    QCOMPARE(list->selectionModel()->selectedIndexes().size(), 5);
    QCOMPARE(colView.selectedPaths().size(), 5);

    // 4. Select all
    colView.selectAll();
    QCOMPARE(colView.selectedPaths().size(), 5);
}

void TestSelection::testGalleryViewSelection()
{
    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    GalleryView galleryView;
    galleryView.setFileSystemModel(model);
    galleryView.resize(600, 400);
    galleryView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&galleryView));

    auto* filmstrip = galleryView.findChild<QListView*>();
    QVERIFY(filmstrip != nullptr);

    QModelIndex idx0 = model->index(0, 0);
    QModelIndex idx1 = model->index(1, 0);
    QVERIFY(idx0.isValid());
    QVERIFY(idx1.isValid());

    QRect r0 = filmstrip->visualRect(idx0);
    QRect r1 = filmstrip->visualRect(idx1);

    // 1. Click first item
    QTest::mouseClick(filmstrip->viewport(), Qt::LeftButton, Qt::NoModifier, r0.center());
    QCOMPARE(filmstrip->selectionModel()->selectedIndexes().size(), 1);

    // 2. Ctrl+Click second item
    QTest::mouseClick(filmstrip->viewport(), Qt::LeftButton, Qt::ControlModifier, r1.center());
    QCOMPARE(filmstrip->selectionModel()->selectedIndexes().size(), 2);
    QCOMPARE(galleryView.selectedFilePaths().size(), 2);

    // 3. Select all
    galleryView.selectAll();
    QCOMPARE(galleryView.selectedFilePaths().size(), 5);
}

void TestSelection::testTabPageMultiSelection()
{
    // Test that TabPage::selectedFilePaths() correctly queries active view
    TabPage tabPage(m_tempDir.path());
    tabPage.resize(600, 400);
    tabPage.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabPage));

    // Mode 0: Icon View
    tabPage.setViewMode(ViewMode::Icon);
    tabPage.selectAll();
    QCOMPARE(tabPage.selectedFilePaths().size(), 5);

    // Mode 1: List View
    tabPage.setViewMode(ViewMode::List);
    tabPage.selectAll();
    QCOMPARE(tabPage.selectedFilePaths().size(), 5);

    // Mode 2: Column View
    tabPage.setViewMode(ViewMode::Column);
    tabPage.selectAll();
    QCOMPARE(tabPage.selectedFilePaths().size(), 5);

    // Mode 3: Gallery View
    tabPage.setViewMode(ViewMode::Gallery);
    tabPage.selectAll();
    QCOMPARE(tabPage.selectedFilePaths().size(), 5);
}

void TestSelection::testNavigationHistory()
{
    QDir dir(m_tempDir.path());
    dir.mkdir("sub1");
    dir.mkdir("sub2");

    QString root = m_tempDir.path();
    QString sub1 = dir.filePath("sub1");
    QString sub2 = dir.filePath("sub2");

    TabPage page(root);
    QVERIFY(!page.canGoBack());
    QVERIFY(!page.canGoForward());

    page.navigateTo(sub1);
    QCOMPARE(page.currentPath(), sub1);
    QVERIFY(page.canGoBack());
    QVERIFY(!page.canGoForward());

    page.navigateTo(sub2);
    QCOMPARE(page.currentPath(), sub2);
    QVERIFY(page.canGoBack());
    QVERIFY(!page.canGoForward());

    page.goBack();
    QCOMPARE(page.currentPath(), sub1);
    QVERIFY(page.canGoBack());
    QVERIFY(page.canGoForward());

    page.goBack();
    QCOMPARE(page.currentPath(), root);
    QVERIFY(!page.canGoBack());
    QVERIFY(page.canGoForward());

    page.goForward();
    QCOMPARE(page.currentPath(), sub1);
    QVERIFY(page.canGoBack());
    QVERIFY(page.canGoForward());

    page.goForward();
    QCOMPARE(page.currentPath(), sub2);
    QVERIFY(page.canGoBack());
    QVERIFY(!page.canGoForward());

    // Test Column View mode with fresh page
    TabPage colPage(root);
    colPage.setViewMode(ViewMode::Column);
    QVERIFY(!colPage.canGoBack());

    colPage.navigateTo(sub1);
    QCOMPARE(colPage.currentPath(), sub1);
    QVERIFY(colPage.canGoBack());

    colPage.goBack();
    QCOMPARE(colPage.currentPath(), root);
    QVERIFY(!colPage.canGoBack());
    QVERIFY(colPage.canGoForward());

    colPage.goForward();
    QCOMPARE(colPage.currentPath(), sub1);
    QVERIFY(colPage.canGoBack());
}

QTEST_MAIN(TestSelection)
#include "test_selection.moc"
