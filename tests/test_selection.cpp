#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QApplication>
#include <QLineEdit>
#include "IconView.h"
#include "ListView.h"
#include "ColumnView.h"
#include "GalleryView.h"
#include "TabPage.h"
#include "TabManager.h"
#include "AppSettings.h"
#include "FileSystemModel.h"
#include "TagManager.h"
#include "AppChooserDialog.h"
#include "TearOffTabBar.h"

class TestSelection : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void testIconViewSelection();
    void testListViewSelection();
    void testColumnViewSelection();
    void testGalleryViewSelection();
    void testTabPageMultiSelection();
    void testNavigationHistory();
    void testSplitTabManager();
    void testSplitViewSync();
    void testTaggedFilesView();
    void testInlineRenamingModel();
    void testSelectByPatternAndInvert();
    void testRenameEditorSelectsStem();
    void testTabManagerCloseAndReopen();
    void testTabTearOffSignal();
    void testColumnWidthPersistence();
    void testAppChooserDialog();

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

void TestSelection::cleanup()
{
    AppSettings::setSettingsFilePath(QString());
}

void TestSelection::testColumnWidthPersistence()
{
    QTemporaryDir directory;
    QTemporaryDir settingsDirectory;
    QVERIFY(directory.isValid());
    QVERIFY(settingsDirectory.isValid());

    AppSettings::setSettingsFilePath(
        settingsDirectory.path() + QStringLiteral("/settings.ini"));

    const QString folderPath = directory.path();
    AppSettings::instance().setColumnViewWidth(folderPath, 321);

    ColumnBrowserList savedWidthList(folderPath);
    QCOMPARE(savedWidthList.preferredWidth(), 321);
    QCOMPARE(savedWidthList.width(), 321);

    AppSettings::instance().removeColumnViewWidth(folderPath);
    ColumnBrowserList resetWidthList(folderPath);
    QCOMPARE(resetWidthList.preferredWidth(), 240);
    QCOMPARE(resetWidthList.width(), 240);
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

void TestSelection::testSplitTabManager()
{
    TabManager tabMgr;
    TabPage* p1 = tabMgr.createTab(m_tempDir.path());
    QVERIFY(p1 != nullptr);
    QCOMPARE(tabMgr.currentPage(), p1);
    QVERIFY(!tabMgr.isSplit());

    // Toggle split
    tabMgr.toggleSplit();
    QVERIFY(tabMgr.isSplit());
    TabPage* p2 = tabMgr.currentPage();
    QVERIFY(p2 != nullptr);
    QVERIFY(p2 != p1);
    QCOMPARE(p2->currentPath(), m_tempDir.path());

    // Toggle back
    tabMgr.toggleSplit();
    QVERIFY(!tabMgr.isSplit());
    QCOMPARE(tabMgr.currentPage(), p1);
}

void TestSelection::testSplitViewSync()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.path() + QStringLiteral("/first");
    const QString secondPath = directory.path() + QStringLiteral("/second");
    QVERIFY(QDir().mkpath(firstPath));
    QVERIFY(QDir().mkpath(secondPath));

    SplitTabWidget split(directory.path());
    split.toggleSplit();
    QVERIFY(split.isSplit());
    QVERIFY(split.primaryPage() != nullptr);
    QVERIFY(split.secondaryPage() != nullptr);
    QVERIFY(split.activePage() == split.secondaryPage());

    split.setSyncEnabled(true);
    QVERIFY(split.isSyncEnabled());

    split.activePage()->navigateTo(firstPath);
    QCOMPARE(split.primaryPage()->currentPath(), firstPath);
    QCOMPARE(split.secondaryPage()->currentPath(), firstPath);

    split.primaryPage()->navigateTo(secondPath);
    QCOMPARE(split.primaryPage()->currentPath(), secondPath);
    QCOMPARE(split.secondaryPage()->currentPath(), secondPath);

    split.setSyncEnabled(false);
    split.secondaryPage()->navigateTo(firstPath);
    QCOMPARE(split.secondaryPage()->currentPath(), firstPath);
    QCOMPARE(split.primaryPage()->currentPath(), secondPath);
}

void TestSelection::testTaggedFilesView()
{
    TabPage page(m_tempDir.path());
    page.setViewMode(ViewMode::Column);
    QCOMPARE(page.viewMode(), ViewMode::Column);

    QString testTag = QStringLiteral("TestTag");
    QString taggedFile = m_createdFiles.first();
    TagManager::instance().addTag(taggedFile, testTag);

    // Navigating or showing tagged files switches out of Column view to display the virtual tag view
    page.showTaggedFiles(testTag);
    QCOMPARE(page.currentPath(), QStringLiteral("tag://TestTag"));
    QVERIFY(page.viewMode() != ViewMode::Column);
    QCOMPARE(page.model()->rowCount(), 1);
    QCOMPARE(page.model()->index(0, 0).data(FileSystemModel::FilePathRole).toString(), taggedFile);

    // Navigating back to directory restores Column view
    page.navigateTo(m_tempDir.path());
    QCOMPARE(page.viewMode(), ViewMode::Column);
    QCOMPARE(page.currentPath(), m_tempDir.path());

    TagManager::instance().removeTag(taggedFile, testTag);
}

void TestSelection::testInlineRenamingModel()
{
    QTemporaryDir renameDir;
    QVERIFY(renameDir.isValid());
    QString testFile = renameDir.filePath("rename_me.txt");
    {
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("content");
    }

    FileSystemModel model;
    model.setRootPath(renameDir.path());
    QCOMPARE(model.rowCount(), 1);

    QModelIndex idx = model.index(0, FileSystemModel::ColName);
    QVERIFY(idx.isValid());
    // Editable flag must be present for inline renaming
    QVERIFY(model.flags(idx) & Qt::ItemIsEditable);

    QSignalSpy renameSpy(&model, &FileSystemModel::itemRenamed);

    // Invalid rename with slash must fail
    QVERIFY(!model.setData(idx, "bad/name.txt", Qt::EditRole));
    QCOMPARE(renameSpy.count(), 0);

    // Valid rename
    QVERIFY(model.setData(idx, "renamed_success.txt", Qt::EditRole));
    QCOMPARE(renameSpy.count(), 1);
    QCOMPARE(renameSpy.at(0).at(0).toString(), testFile);
    QCOMPARE(renameSpy.at(0).at(1).toString(), renameDir.filePath("renamed_success.txt"));

    QVERIFY(QFile::exists(renameDir.filePath("renamed_success.txt")));
    QVERIFY(!QFile::exists(testFile));
}

void TestSelection::testSelectByPatternAndInvert()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString alpha = directory.filePath(QStringLiteral("alpha.txt"));
    const QString beta = directory.filePath(QStringLiteral("beta.txt"));
    const QString gamma = directory.filePath(QStringLiteral("gamma.png"));
    {
        QFile file(alpha);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("a");
    }
    {
        QFile file(beta);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("b");
    }
    {
        QFile file(gamma);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("g");
    }

    TabPage page(directory.path());
    page.setViewMode(ViewMode::Icon);
    page.resize(600, 400);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QTRY_COMPARE_WITH_TIMEOUT(page.model()->rowCount(), 3, 1000);

    page.selectByPattern(QStringLiteral("*.txt"));
    QStringList selected = page.selectedFilePaths();
    QCOMPARE(selected.size(), 2);
    QVERIFY(selected.contains(alpha));
    QVERIFY(selected.contains(beta));

    page.invertSelection();
    selected = page.selectedFilePaths();
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first(), gamma);
}

void TestSelection::testRenameEditorSelectsStem()
{
    QTemporaryDir renameDir;
    QVERIFY(renameDir.isValid());

    const QString filePath = renameDir.filePath(QStringLiteral("rename_me.txt"));
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("content");
    }
    const QString folderPath = renameDir.filePath(QStringLiteral("folder.name"));
    QVERIFY(QDir(renameDir.path()).mkdir(QStringLiteral("folder.name")));

    auto* model = new FileSystemModel(this);
    model->setRootPath(renameDir.path());

    IconView iconView;
    iconView.setFileSystemModel(model);
    iconView.resize(600, 400);
    iconView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&iconView));

    int fileRow = -1;
    int folderRow = -1;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, FileSystemModel::ColName);
        const QString path = index.data(FileSystemModel::FilePathRole).toString();
        if (path == filePath) fileRow = row;
        if (path == folderPath) folderRow = row;
    }
    QVERIFY(fileRow >= 0);
    QVERIFY(folderRow >= 0);

    // Files: select the basename but leave the extension unselected.
    const QModelIndex fileIndex = model->index(fileRow, FileSystemModel::ColName);
    iconView.setCurrentIndex(fileIndex);
    iconView.edit(fileIndex);
    QLineEdit* fileEditor = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((fileEditor = iconView.findChild<QLineEdit*>()) != nullptr, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(fileEditor->selectedText(), QStringLiteral("rename_me"), 1000);
    QTest::keyClick(fileEditor, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(iconView.findChild<QLineEdit*>() == nullptr, 1000);

    // Directories: there is no extension, so the whole name stays selected.
    const QModelIndex folderIndex = model->index(folderRow, FileSystemModel::ColName);
    iconView.setCurrentIndex(folderIndex);
    iconView.edit(folderIndex);
    QLineEdit* folderEditor = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((folderEditor = iconView.findChild<QLineEdit*>()) != nullptr, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(folderEditor->selectedText(), QStringLiteral("folder.name"), 1000);
    QTest::keyClick(folderEditor, Qt::Key_Escape);
}

void TestSelection::testTabManagerCloseAndReopen()
{
    TabManager tabMgr;
    tabMgr.createTab(m_tempDir.path());
    QCOMPARE(tabMgr.count(), 1);

    QTemporaryDir subDir;
    QVERIFY(subDir.isValid());
    tabMgr.createTab(subDir.path());
    QCOMPARE(tabMgr.count(), 2);
    QVERIFY(!tabMgr.canReopenClosedTab());

    // Close second tab
    tabMgr.closeTab(1);
    QCOMPARE(tabMgr.count(), 1);
    QVERIFY(tabMgr.canReopenClosedTab());

    // Reopen closed tab
    TabPage* reopened = tabMgr.reopenLastClosedTab();
    QVERIFY(reopened != nullptr);
    QCOMPARE(tabMgr.count(), 2);
    QCOMPARE(reopened->currentPath(), subDir.path());
    QVERIFY(!tabMgr.canReopenClosedTab());
}

void TestSelection::testTabTearOffSignal()
{
    TearOffTabBar tabBar;
    tabBar.addTab(QStringLiteral("Tab 1"));
    tabBar.addTab(QStringLiteral("Tab 2"));
    tabBar.resize(300, 30);
    tabBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabBar));

    QSignalSpy spy(&tabBar, &TearOffTabBar::tearOffRequested);
    const QPoint pressPosition = tabBar.tabRect(0).center();
    QTest::mousePress(&tabBar, Qt::LeftButton, Qt::NoModifier, pressPosition);

    const QPointF tearOffPosition(pressPosition.x(), tabBar.height() + 50);
    QMouseEvent tearOffMove(QEvent::MouseMove, tearOffPosition,
                            QPointF(tabBar.mapToGlobal(tearOffPosition.toPoint())),
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&tabBar, &tearOffMove);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
    QTest::mouseRelease(&tabBar, Qt::LeftButton, Qt::NoModifier, pressPosition);

    spy.clear();
    QTest::mousePress(&tabBar, Qt::LeftButton, Qt::NoModifier, pressPosition);
    const QPointF horizontalPosition(tabBar.width() + 60, pressPosition.y());
    QMouseEvent horizontalMove(QEvent::MouseMove, horizontalPosition,
                               QPointF(tabBar.mapToGlobal(horizontalPosition.toPoint())),
                               Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&tabBar, &horizontalMove);
    QCOMPARE(spy.count(), 0);
    QTest::mouseRelease(&tabBar, Qt::LeftButton, Qt::NoModifier, pressPosition);
}

void TestSelection::testAppChooserDialog()
{
    AppChooserDialog dlg(QStringLiteral("text/plain"), QStringLiteral("notes.txt"));
    QVERIFY(dlg.customCommand().isEmpty());
    QVERIFY(!dlg.makeDefault());
    // Should have selected an app from the system if any desktop apps exist
    if (!DesktopAppsManager::allDesktopApps().isEmpty()) {
        QVERIFY(!dlg.selectedAppId().isEmpty());
    }
}

QTEST_MAIN(TestSelection)
#include "test_selection.moc"
