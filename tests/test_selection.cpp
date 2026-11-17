#include "AppChooserDialog.h"
#include "AppSettings.h"
#include "ColumnView.h"
#include "FileSystemModel.h"
#include "GalleryView.h"
#include "GetInfoDialog.h"
#include "IconView.h"
#include "ListView.h"
#include "PreviewManager.h"
#include "PreviewPane.h"
#include "RecentFilesManager.h"
#include "TabManager.h"
#include "TabPage.h"
#include "TagManager.h"
#include "TearOffTabBar.h"
#include "TerminalPanel.h"
#include "Toolbar.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QLineEdit>
#include <QPixmapCache>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>

class TestSelection : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

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
    void testListViewColumnSettings();
    void testListViewSortPersistence();
    void testIconViewViewOptions();
    void testManualIconPositions();
    void testTabPageSmartFolder();
    void testPreviewPaneToggle();
    void testMultiItemGetInfo();
    void testToolbarSearchToggle();
    void testRecentFilesView();
    void testRecentFolderHistory();
    void testOfficePreviewDetection();
    void testOfficePreviewConversion();
    void testPdfAsyncRendering();
    void testPdfAsyncPageCount();
    void testPdfAsyncFind();
    void testThumbnailRetryAfterFileChanges();
    void testIconSizeDoesNotClearGlobalPixmapCache();
    void testTerminalScrollbackIsCapped();
    void testTerminalAnsiRendering();
    void testTerminalCtrlCInterruptsProcessGroup();
    void testFolderViewModeFallsBackToGlobal();
    void testFolderViewModeNeverLeaksBetweenFolders();
    void testFolderOverrideDoesNotInheritToSubfolder();
    void testChangingViewInFolderRecordsOverride();
    void testChangingViewWithGlobalScope();
    void testVirtualViewSwitchDoesNotPersist();
    void testClearOverrideFallsBackImmediately();
    void testSplitPanesKeepIndependentModes();
    void testSessionRestoreDoesNotPersist();
    void testAppChooserDialog();

private:
    QTemporaryDir m_tempDir;
    QTemporaryDir m_settingsDir;
    QStringList m_createdFiles;
};

void TestSelection::initTestCase() {
    QVERIFY(m_tempDir.isValid());
    QVERIFY(m_settingsDir.isValid());
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

void TestSelection::cleanup() {
    AppSettings::setSettingsFilePath(QString());
}

// Every test gets its own settings file: per-folder view modes default to on,
// so a test that changes the view would otherwise write to the real config.
void TestSelection::init() {
    static int counter = 0;
    AppSettings::setSettingsFilePath(
        m_settingsDir.filePath(QStringLiteral("selection-%1.ini").arg(++counter)));
}

void TestSelection::testColumnWidthPersistence() {
    QTemporaryDir directory;
    QTemporaryDir settingsDirectory;
    QVERIFY(directory.isValid());
    QVERIFY(settingsDirectory.isValid());

    AppSettings::setSettingsFilePath(settingsDirectory.path() + QStringLiteral("/settings.ini"));

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

void TestSelection::testIconViewSelection() {
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
    // QListView's selection model exposes every model column for each row.
    const int expectedIndexes = model->rowCount() * model->columnCount();
    QCOMPARE(iconView.selectionModel()->selectedIndexes().size(), expectedIndexes);
}

void TestSelection::testListViewSelection() {
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

void TestSelection::testColumnViewSelection() {
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

void TestSelection::testGalleryViewSelection() {
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

void TestSelection::testTabPageMultiSelection() {
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

void TestSelection::testNavigationHistory() {
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

void TestSelection::testSplitTabManager() {
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

void TestSelection::testSplitViewSync() {
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

void TestSelection::testTaggedFilesView() {
    TabPage page(m_tempDir.path());
    page.setViewMode(ViewMode::Column);
    QCOMPARE(page.viewMode(), ViewMode::Column);

    QString testTag = QStringLiteral("TestTag");
    QString taggedFile = m_createdFiles.first();
    TagManager::instance().addTag(taggedFile, testTag);

    // Navigating or showing tagged files switches out of Column view to display the virtual tag
    // view
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

void TestSelection::testInlineRenamingModel() {
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

void TestSelection::testSelectByPatternAndInvert() {
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

void TestSelection::testRenameEditorSelectsStem() {
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
        if (path == filePath)
            fileRow = row;
        if (path == folderPath)
            folderRow = row;
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

void TestSelection::testTabManagerCloseAndReopen() {
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

void TestSelection::testTabTearOffSignal() {
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
                            QPointF(tabBar.mapToGlobal(tearOffPosition.toPoint())), Qt::NoButton,
                            Qt::LeftButton, Qt::NoModifier);
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

void TestSelection::testListViewColumnSettings() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    AppSettings::setSettingsFilePath(settingsDirectory.path() + QStringLiteral("/settings.ini"));
    AppSettings::instance().setListVisibleColumns({QStringLiteral("name"), QStringLiteral("size")});

    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    ListView listView;
    listView.setFileSystemModel(model);

    QVERIFY(!listView.isColumnHidden(FileSystemModel::ColName));
    QVERIFY(!listView.isColumnHidden(FileSystemModel::ColSize));
    QVERIFY(listView.isColumnHidden(FileSystemModel::ColDateModified));
    QVERIFY(listView.isColumnHidden(FileSystemModel::ColKind));

    AppSettings::instance().setListVisibleColumns(
        {QStringLiteral("name"), QStringLiteral("kind"), QStringLiteral("permissions")});
    listView.applyColumnConfiguration();
    QVERIFY(!listView.isColumnHidden(FileSystemModel::ColKind));
    QVERIFY(!listView.isColumnHidden(FileSystemModel::ColPermissions));
    QVERIFY(listView.isColumnHidden(FileSystemModel::ColSize));
}

void TestSelection::testListViewSortPersistence() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    AppSettings::setSettingsFilePath(settingsDirectory.path() + QStringLiteral("/settings.ini"));

    // A restored non-default sort order must survive attaching the list view.
    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());
    model->sort(FileSystemModel::ColDateModified, Qt::DescendingOrder);

    ListView listView;
    listView.setFileSystemModel(model);

    QCOMPARE(model->sortColumn(), static_cast<int>(FileSystemModel::ColDateModified));
    QCOMPARE(model->sortOrder(), Qt::DescendingOrder);
    QCOMPARE(listView.header()->sortIndicatorSection(),
             static_cast<int>(FileSystemModel::ColDateModified));
    QCOMPARE(listView.header()->sortIndicatorOrder(), Qt::DescendingOrder);
    QVERIFY(listView.header()->isSortIndicatorShown());

    // Sorting through the View menu / shortcuts updates the header arrow too.
    model->sort(FileSystemModel::ColSize, Qt::AscendingOrder);
    QCOMPARE(listView.header()->sortIndicatorSection(), static_cast<int>(FileSystemModel::ColSize));
    QCOMPARE(listView.header()->sortIndicatorOrder(), Qt::AscendingOrder);

    // Clicking a header keeps model and view in sync.
    listView.header()->setSortIndicator(FileSystemModel::ColName, Qt::DescendingOrder);
    QCOMPARE(model->sortColumn(), static_cast<int>(FileSystemModel::ColName));
    QCOMPARE(model->sortOrder(), Qt::DescendingOrder);

    // Re-attaching the same model must not reset the order either.
    listView.setFileSystemModel(model);
    QCOMPARE(model->sortColumn(), static_cast<int>(FileSystemModel::ColName));
    QCOMPARE(model->sortOrder(), Qt::DescendingOrder);
    QCOMPARE(listView.header()->sortIndicatorSection(), static_cast<int>(FileSystemModel::ColName));

    // End-to-end: a TabPage restored from settings keeps the saved sort order
    // instead of silently falling back to Name/Ascending.
    AppSettings::instance().setSortColumn(FileSystemModel::ColSize);
    AppSettings::instance().setSortOrder(Qt::DescendingOrder);
    TabPage page(m_tempDir.path());
    page.setViewMode(ViewMode::List);

    auto* pageModel = page.model();
    QVERIFY(pageModel);
    QCOMPARE(pageModel->sortColumn(), static_cast<int>(FileSystemModel::ColSize));
    QCOMPARE(pageModel->sortOrder(), Qt::DescendingOrder);
    QCOMPARE(AppSettings::instance().sortColumn(), static_cast<int>(FileSystemModel::ColSize));
    QCOMPARE(AppSettings::instance().sortOrder(), Qt::DescendingOrder);

    auto* pageList = page.findChild<ListView*>();
    QVERIFY(pageList);
    QCOMPARE(pageList->header()->sortIndicatorSection(),
             static_cast<int>(FileSystemModel::ColSize));
    QCOMPARE(pageList->header()->sortIndicatorOrder(), Qt::DescendingOrder);
}

void TestSelection::testIconViewViewOptions() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    AppSettings::setSettingsFilePath(settingsDirectory.path() + QStringLiteral("/settings.ini"));

    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    IconView iconView;
    iconView.setFileSystemModel(model);
    iconView.setFolderPath(m_tempDir.path());

    FolderViewOptions options;
    options.iconSize = 80;
    options.textSize = 13;
    options.labelPosition = 1;
    options.gridSpacing = 20;
    options.showItemInfo = true;
    options.showIconPreview = false;
    options.manualArrangement = true;
    iconView.applyViewOptions(options);

    QCOMPARE(iconView.iconSize(), QSize(80, 80));
    QCOMPARE(iconView.spacing(), 20);
    QCOMPARE(iconView.movement(), QListView::Free);

    options.manualArrangement = false;
    options.labelPosition = 0;
    iconView.applyViewOptions(options);
    QCOMPARE(iconView.movement(), QListView::Static);
}

void TestSelection::testManualIconPositions() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    AppSettings::setSettingsFilePath(settingsDirectory.path() + QStringLiteral("/settings.ini"));

    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    struct TestableIconView : IconView {
        using QListView::rectForIndex;
        using QListView::setPositionForIndex;
    } iconView;
    iconView.setFileSystemModel(model);
    iconView.resize(600, 400);
    iconView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&iconView));
    iconView.setFolderPath(m_tempDir.path());

    FolderViewOptions options;
    options.manualArrangement = true;
    iconView.applyViewOptions(options);

    const QModelIndex index = model->index(0, FileSystemModel::ColName);
    QVERIFY(index.isValid());
    const QPoint target(64, 80);
    iconView.setPositionForIndex(target, index);

    // Leaving and re-entering the folder persists and restores the position.
    iconView.setFolderPath(QDir::tempPath());
    iconView.setFolderPath(m_tempDir.path());
    QTest::qWait(100);
    QCOMPARE(iconView.rectForIndex(index).topLeft(), target);
}

void TestSelection::testTabPageSmartFolder() {
    QTemporaryDir settingsDirectory;
    QTemporaryDir searchDirectory;
    QVERIFY(settingsDirectory.isValid());
    QVERIFY(searchDirectory.isValid());
    AppSettings::setSettingsFilePath(settingsDirectory.path() + QStringLiteral("/settings.ini"));

    QFile hit(searchDirectory.filePath(QStringLiteral("needle.txt")));
    QVERIFY(hit.open(QIODevice::WriteOnly));
    hit.write("needle");
    hit.close();
    QFile miss(searchDirectory.filePath(QStringLiteral("other.txt")));
    QVERIFY(miss.open(QIODevice::WriteOnly));
    miss.write("other");
    miss.close();

    SmartFolder folder;
    folder.name = QStringLiteral("Needle");
    folder.filter = SearchQuery::parse(QStringLiteral("needle"));
    folder.filter.searchPath = searchDirectory.path();
    folder.filter.recursive = false;
    AppSettings::instance().saveSmartFolder(folder);

    TabPage page(QDir::homePath());
    page.showSmartFolder(folder);
    QCOMPARE(page.currentPath(), QStringLiteral("smart://Needle"));
    QCOMPARE(page.searchText(), QStringLiteral("needle"));
    QTRY_COMPARE_WITH_TIMEOUT(page.model()->rowCount(), 1, 5000);
    QCOMPARE(page.model()->index(0, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("needle.txt"));

    page.navigateTo(searchDirectory.path());
    QVERIFY(!page.currentPath().startsWith(QStringLiteral("smart://")));
}

void TestSelection::testPreviewPaneToggle() {
    TabPage page(m_tempDir.path());
    auto* pane = page.findChild<PreviewPane*>();
    QVERIFY(pane);
    page.setViewMode(ViewMode::List);

    page.setPreviewPaneVisible(false);
    QVERIFY(!page.isPreviewPaneVisible());
    QVERIFY(pane->isHidden());

    page.setPreviewPaneVisible(true);
    QVERIFY(page.isPreviewPaneVisible());
    QVERIFY(!pane->isHidden());

    page.setViewMode(ViewMode::Column);
    QVERIFY(pane->isHidden());

    page.setViewMode(ViewMode::List);
    QVERIFY(!pane->isHidden());
}

void TestSelection::testMultiItemGetInfo() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = dir.filePath(QStringLiteral("first.txt"));
    const QString second = dir.filePath(QStringLiteral("second.txt"));
    for (const QString& path : {first, second}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
    }

    GetInfoDialog dialog({first, second});
    QVERIFY(dialog.windowTitle().contains(QStringLiteral("2 Items")));
    QVERIFY(!dialog.findChildren<QCheckBox*>().isEmpty());
    QVERIFY(!dialog.findChildren<QLineEdit*>().isEmpty());

    auto* tabs = dialog.findChild<QTabWidget*>();
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QStringLiteral("General"));
    QCOMPARE(tabs->tabText(1), QStringLiteral("Permissions"));
}

void TestSelection::testToolbarSearchToggle() {
    Toolbar toolbar;
    auto* capsule = toolbar.findChild<QWidget*>(QStringLiteral("pathCapsule"));
    QVERIFY(capsule);
    QCOMPARE(toolbar.breadcrumbBar()->parentWidget(), capsule);
    QVERIFY(!toolbar.isSearchVisible());

    toolbar.focusSearch();
    QVERIFY(toolbar.isSearchVisible());

    toolbar.setSearchText(QStringLiteral("kind:image"));
    QCOMPARE(toolbar.searchQuery(), QStringLiteral("kind:image"));

    toolbar.setSearchVisible(false);
    QVERIFY(!toolbar.isSearchVisible());
}

void TestSelection::testRecentFilesView() {
    QTemporaryDir settingsDir;
    QTemporaryDir filesDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(filesDir.isValid());
    AppSettings::setSettingsFilePath(settingsDir.path() + QStringLiteral("/settings.ini"));
    RecentFilesManager::setStoragePathForTesting(filesDir.path() + QStringLiteral("/recent.xbel"));

    const QString first = filesDir.filePath(QStringLiteral("first.txt"));
    const QString second = filesDir.filePath(QStringLiteral("second.txt"));
    for (const QString& path : {first, second}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();
    }
    QVERIFY(RecentFilesManager::add(first, QStringLiteral("text/plain")));
    QTest::qWait(5);
    QVERIFY(RecentFilesManager::add(second, QStringLiteral("text/plain")));

    TabPage page(QDir::homePath());
    page.showRecentFiles();
    QCOMPARE(page.currentPath(), QStringLiteral("recent://Recents"));
    QCOMPARE(page.model()->rowCount(), 2);
    QCOMPARE(page.model()->index(0, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("second.txt"));
    QCOMPARE(page.model()->index(1, 0).data(FileSystemModel::FileNameRole).toString(),
             QStringLiteral("first.txt"));

    RecentFilesManager::setStoragePathForTesting(QString());
}

void TestSelection::testRecentFolderHistory() {
    QTemporaryDir settingsDir;
    QTemporaryDir folder;
    QVERIFY(settingsDir.isValid());
    QVERIFY(folder.isValid());
    AppSettings::setSettingsFilePath(settingsDir.path() + QStringLiteral("/settings.ini"));

    TabPage page(QDir::homePath());
    page.navigateTo(folder.path());
    QVERIFY(AppSettings::instance().recentFolders().contains(folder.path()));

    AppSettings::instance().clearRecentFolders();
    QVERIFY(AppSettings::instance().recentFolders().isEmpty());
}

void TestSelection::testOfficePreviewDetection() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString docx = dir.filePath(QStringLiteral("report.docx"));
    const QString odt = dir.filePath(QStringLiteral("notes.odt"));
    const QString text = dir.filePath(QStringLiteral("notes.txt"));
    for (const QString& path : {docx, odt, text}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    auto& previews = PreviewManager::instance();
    QVERIFY(previews.isOffice(docx));
    QVERIFY(previews.isOffice(odt));
    QVERIFY(!previews.isOffice(text));
}

void TestSelection::testOfficePreviewConversion() {
    const QString program = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    if (program.isEmpty()) {
        QSKIP("LibreOffice is not installed");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("office-source.txt"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("Office preview conversion test");
    sourceFile.close();

    QProcess convert;
    convert.start(program,
                  {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                   QStringLiteral("--nofirststartwizard"), QStringLiteral("--convert-to"),
                   QStringLiteral("docx"), QStringLiteral("--outdir"), dir.path(), source});
    if (!convert.waitForFinished(20000) || convert.exitCode() != 0) {
        QSKIP("LibreOffice could not create a test document");
    }

    const QString docx = dir.filePath(QStringLiteral("office-source.docx"));
    QVERIFY2(QFileInfo(docx).size() > 0, "test document was not created");

    auto& previews = PreviewManager::instance();
    QString error;
    const QString pdf = previews.officePdfPath(docx, &error);
    QVERIFY2(!pdf.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo(pdf).size() > 0);
    QVERIFY(QFile::exists(pdf));

    QVERIFY(previews.pdfPageCount(pdf) >= 1);
    const QPixmap rendered = previews.getPdfPreview(pdf, QSize(320, 240), 0);
    QVERIFY(!rendered.isNull());

    // The UI path requests conversion asynchronously.
    QSignalSpy readySpy(&previews, &PreviewManager::officePreviewReady);
    previews.requestOfficePreview(docx);
    QVERIFY(readySpy.wait(20000));
    QCOMPARE(readySpy.at(0).at(0).toString(), docx);
    QVERIFY(QFile::exists(readySpy.at(0).at(1).toString()));
}

void TestSelection::testPdfAsyncRendering() {
    auto& previews = PreviewManager::instance();

    // A missing document must still report back, asynchronously, with a null
    // pixmap instead of crashing or hanging the caller.
    bool returnedBeforeCallback = false;
    bool callbackRan = false;
    QPixmap received;
    bool callFinished = false;

    previews.requestPdfPage(QStringLiteral("/nonexistent/missing.pdf"), QSize(200, 150), 0,
                            [&](const QPixmap& pixmap) {
                                returnedBeforeCallback = callFinished;
                                callbackRan = true;
                                received = pixmap;
                            });
    callFinished = true;

    // Rendering must not happen inline on the caller's thread.
    QVERIFY2(!callbackRan, "requestPdfPage must not invoke its callback synchronously");
    QTRY_VERIFY_WITH_TIMEOUT(callbackRan, 10000);
    QVERIFY2(returnedBeforeCallback, "callback should run after the request returns");
    QVERIFY(received.isNull());

    // A newer request for the same document supersedes a queued one, so a slow
    // render cannot paint a page the user already navigated away from.
    QString order;
    previews.requestPdfPage(QStringLiteral("/nonexistent/missing.pdf"), QSize(200, 150), 0,
                            [&order](const QPixmap&) { order += QLatin1Char('A'); });
    previews.requestPdfPage(QStringLiteral("/nonexistent/missing.pdf"), QSize(200, 150), 0,
                            [&order](const QPixmap&) { order += QLatin1Char('B'); });
    QTRY_VERIFY_WITH_TIMEOUT(order.endsWith(QLatin1Char('B')), 10000);
    QVERIFY2(!order.contains(QLatin1Char('A')), "superseded render must be discarded");
}

void TestSelection::testPdfAsyncPageCount() {
    auto& previews = PreviewManager::instance();

    bool callbackRan = false;
    int pages = -99;
    previews.requestPdfPageCount(QStringLiteral("/nonexistent/missing.pdf"), [&](int count) {
        pages = count;
        callbackRan = true;
    });
    QVERIFY2(!callbackRan, "requestPdfPageCount must not run inline");
    QTRY_VERIFY_WITH_TIMEOUT(callbackRan, 10000);
    QCOMPARE(pages, 0);

    // An empty path is answered immediately without touching the disk.
    bool emptyAnswered = false;
    previews.requestPdfPageCount(QString(), [&](int count) { emptyAnswered = (count == 0); });
    QVERIFY(emptyAnswered);
}

void TestSelection::testPdfAsyncFind() {
    auto& previews = PreviewManager::instance();

    bool callbackRan = false;
    int found = 0;
    previews.requestPdfFind(QStringLiteral("/nonexistent/missing.pdf"), QStringLiteral("needle"),
                            [&](int page) {
                                found = page;
                                callbackRan = true;
                            });
    QVERIFY2(!callbackRan, "requestPdfFind must not run inline");
    QTRY_VERIFY_WITH_TIMEOUT(callbackRan, 10000);
    QCOMPARE(found, -1);

    // An empty search term is rejected without scanning anything.
    bool emptyAnswered = false;
    previews.requestPdfFind(QStringLiteral("/nonexistent/missing.pdf"), QString(),
                            [&](int page) { emptyAnswered = (page == -1); });
    QVERIFY(emptyAnswered);
}

void TestSelection::testThumbnailRetryAfterFileChanges() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto& previews = PreviewManager::instance();

    const QString path = dir.filePath(QStringLiteral("broken.png"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a png");
    }
    const qint64 brokenModified = QFileInfo(path).lastModified().toMSecsSinceEpoch();

    int readyCount = 0;
    auto connection = connect(&previews, &PreviewManager::thumbnailReady, this,
                              [&readyCount](const QString&) { ++readyCount; });

    // A file that cannot be thumbnailed must not produce a thumbnail...
    previews.iconThumbnail(path, 128);
    QTest::qWait(700);
    QCOMPARE(readyCount, 0);

    // ...and repeated repaints must not queue the same doomed work again.
    for (int i = 0; i < 4; ++i) {
        previews.iconThumbnail(path, 128);
    }
    QTest::qWait(400);
    QCOMPARE(readyCount, 0);

    // Replacing the file with a valid image changes its mtime, so the failure
    // record no longer applies and the thumbnail must be regenerated. Before
    // this fix the path stayed blacklisted for the whole session.
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path, "PNG"));
    }
    const qint64 fixedModified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    QVERIFY2(fixedModified != brokenModified, "fixture must change the mtime");

    previews.iconThumbnail(path, 128);
    QTRY_VERIFY_WITH_TIMEOUT(readyCount >= 1, 10000);

    disconnect(connection);
}

void TestSelection::testIconSizeDoesNotClearGlobalPixmapCache() {
    // Changing the icon size must not flush Qt's global pixmap cache: the
    // delegate already keys its entries by scaled size and DPR, so unrelated
    // toolbar, sidebar and menu pixmaps would be evicted for nothing.
    QPixmap marker(32, 32);
    marker.fill(Qt::magenta);
    const QString markerKey = QStringLiteral("test-selection#marker-pixmap");
    QPixmapCache::insert(markerKey, marker);

    auto* model = new FileSystemModel(this);
    model->setRootPath(m_tempDir.path());

    IconView view;
    view.setFileSystemModel(model);
    view.setIconSizeValue(72); // triggers the delegate's setIconSize
    view.setIconSizeValue(96);

    QPixmap retrieved;
    QVERIFY2(QPixmapCache::find(markerKey, &retrieved),
             "changing the icon size must not clear the global QPixmapCache");
    QCOMPARE(retrieved.size(), QSize(32, 32));

    QPixmapCache::remove(markerKey);
}

void TestSelection::testTerminalScrollbackIsCapped() {
    // Without a cap a chatty command appends forever and every further insert
    // re-lays out an ever-growing document, freezing the window.
    TerminalPanel panel;
    auto* output = panel.findChild<QTextEdit*>();
    QVERIFY(output);
    QCOMPARE(output->document()->maximumBlockCount(), 5000);
}

void TestSelection::testTerminalAnsiRendering() {
    TerminalPanel panel;
    panel.resize(700, 400);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));

    auto* input = panel.findChild<QLineEdit*>();
    auto* output = panel.findChild<QTextEdit*>();
    QVERIFY(input);
    QVERIFY(output);

    // Colour codes must be interpreted, not printed literally. Wait for the
    // child process to actually produce its output, not just for the echo.
    input->setText(QStringLiteral("printf '\\033[32mGREENTEXT\\033[0m plain\\n'"));
    QMetaObject::invokeMethod(&panel, "onCommandEntered", Qt::DirectConnection);
    QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("GREENTEXT plain")),
                             15000);

    const QString text = output->toPlainText();
    // The echoed command line legitimately contains the escape text, so only
    // inspect the rendered output line that follows it.
    QVERIFY2(!text.contains(QStringLiteral("\x1b")), "raw ESC byte leaked into output");

    const qsizetype commandEnd = text.indexOf(QStringLiteral("GREENTEXT plain"));
    QVERIFY(commandEnd >= 0);
    const QString rendered = text.mid(commandEnd, 15);
    QVERIFY2(!rendered.contains(QStringLiteral("[32m")), "escape sequence leaked into output");

    // The coloured run must carry an explicit foreground colour.
    bool foundColoured = false;
    for (QTextBlock block = output->document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.text().contains(QStringLiteral("GREENTEXT"))) {
                foundColoured = fragment.charFormat().hasProperty(QTextFormat::ForegroundBrush);
            }
        }
    }
    QVERIFY2(foundColoured, "ANSI colour was not applied to the text");

    // Unsupported sequences must be swallowed without garbling the text.
    // The echoed command line still shows the literal escape, so check the
    // rendered output line that starts with the marker.
    input->setText(QStringLiteral("printf '\\033[999mZZSTARTZZ ZZENDZZ\\n'"));
    QMetaObject::invokeMethod(&panel, "onCommandEntered", Qt::DirectConnection);
    QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains(QStringLiteral("ZZSTARTZZ ZZENDZZ")),
                             15000);

    const QString full = output->toPlainText();
    const qsizetype renderedStart = full.lastIndexOf(QStringLiteral("ZZSTARTZZ ZZENDZZ"));
    QVERIFY(renderedStart >= 0);
    const QString renderedLine = full.mid(renderedStart, 19);
    QVERIFY2(renderedLine.startsWith(QStringLiteral("ZZSTARTZZ ZZENDZZ")),
             qPrintable(QStringLiteral("unexpected output: %1").arg(renderedLine)));
    QVERIFY2(!renderedLine.contains(QStringLiteral("999m")),
             "unsupported escape was not swallowed");
}

void TestSelection::testTerminalCtrlCInterruptsProcessGroup() {
    TerminalPanel panel;
    panel.resize(700, 400);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.focusInput();

    auto* input = panel.findChild<QLineEdit*>();
    auto* output = panel.findChild<QTextEdit*>();
    QVERIFY(input);
    QVERIFY(output);

    // A multi-command line forces `sh` to fork, so the child is a separate
    // process. This is what exposes the bug: killing only the shell orphans the
    // child, which keeps running invisibly.
    const QString marker = QStringLiteral("ZZ_CHILD_SURVIVED_MARKER");
    input->setText(QStringLiteral("sleep 30; echo %1").arg(marker));
    QMetaObject::invokeMethod(&panel, "onCommandEntered", Qt::DirectConnection);

    auto* process = panel.findChild<QProcess*>();
    QVERIFY(process);
    QTRY_COMPARE_WITH_TIMEOUT(process->state(), QProcess::Running, 10000);

    // Wait until `sh` has actually forked its child, otherwise the shell may be
    // killed before the child exists and there is nothing to orphan.
    const qint64 leaderPid = process->processId();
    QVERIFY(leaderPid > 0);
    QList<qint64> childPids;
    for (int attempt = 0; attempt < 40 && childPids.isEmpty(); ++attempt) {
        QTest::qWait(100);
        QProcess probe;
        probe.start(QStringLiteral("/bin/sh"),
                    {QStringLiteral("-c"), QStringLiteral("pgrep -P %1 || true").arg(leaderPid)});
        probe.waitForFinished(2000);
        const QStringList lines = QString::fromUtf8(probe.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            bool ok = false;
            const qint64 pid = line.trimmed().toLongLong(&ok);
            if (ok && pid > 0) {
                childPids.append(pid);
            }
        }
    }
    QVERIFY2(!childPids.isEmpty(), "the shell never forked a child");

    QTest::keyClick(input, Qt::Key_C, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(process->state(), QProcess::NotRunning, 15000);
    QVERIFY2(output->toPlainText().contains(QStringLiteral("^C")), "no interrupt feedback shown");

    // The forked child must be gone too. Check /proc state rather than kill -0:
    // a killed child is only guaranteed to be reaped by its parent, and under a
    // container's non-reaping PID 1 it lingers as a zombie ('Z') for which
    // kill -0 still reports success. A zombie is dead, so that counts as gone.
    QTest::qWait(700);
    for (qint64 childPid : childPids) {
        QProcess state;
        state.start(QStringLiteral("/bin/sh"),
                    {QStringLiteral("-c"),
                     QStringLiteral("if [ -r /proc/%1/stat ]; then "
                                    "awk '{print $3}' /proc/%1/stat; else echo gone; fi")
                         .arg(childPid)});
        state.waitForFinished(2000);
        const QString procState = QString::fromUtf8(state.readAllStandardOutput()).trimmed();
        // 'Z' (zombie) and 'X' (dead) mean the process has terminated; only a
        // live state such as R/S/D/T means it survived the interrupt.
        QVERIFY2(procState.isEmpty() || procState == QStringLiteral("gone") ||
                     procState == QStringLiteral("Z") || procState == QStringLiteral("X"),
                 qPrintable(QStringLiteral("child process %1 survived Ctrl+C (state '%2'); the "
                                           "process group was not signalled")
                                .arg(childPid)
                                .arg(procState)));
    }
}

void TestSelection::testFolderViewModeFallsBackToGlobal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString photos = root.filePath(QStringLiteral("photos"));
    const QString docs = root.filePath(QStringLiteral("docs"));
    QVERIFY(QDir().mkpath(photos));
    QVERIFY(QDir().mkpath(docs));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column); // the global default
    settings.setFolderViewModesEnabled(true);
    settings.setFolderViewMode(photos, ViewMode::Gallery);

    TabPage page(photos);
    QCOMPARE(page.viewMode(), ViewMode::Gallery);

    // The reported bug: docs used to keep Gallery because the mode of the
    // folder just left was never replaced by the default.
    page.navigateTo(docs);
    QCOMPARE(page.viewMode(), ViewMode::Column);
}

void TestSelection::testFolderViewModeNeverLeaksBetweenFolders() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString photos = root.filePath(QStringLiteral("photos"));
    const QString docs = root.filePath(QStringLiteral("docs"));
    QVERIFY(QDir().mkpath(photos));
    QVERIFY(QDir().mkpath(docs));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    settings.setFolderViewMode(photos, ViewMode::Gallery);

    TabPage page(photos);
    QCOMPARE(page.viewMode(), ViewMode::Gallery);
    page.navigateTo(docs);
    QCOMPARE(page.viewMode(), ViewMode::Column);
    page.navigateTo(photos);
    QCOMPARE(page.viewMode(), ViewMode::Gallery);
    page.navigateTo(docs);
    QCOMPARE(page.viewMode(), ViewMode::Column);
}

void TestSelection::testFolderOverrideDoesNotInheritToSubfolder() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString photos = root.filePath(QStringLiteral("photos"));
    const QString year = photos + QStringLiteral("/2024");
    QVERIFY(QDir().mkpath(year));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    settings.setFolderViewMode(photos, ViewMode::Gallery);

    TabPage page(photos);
    QCOMPARE(page.viewMode(), ViewMode::Gallery);

    // Exact-folder only: a child without its own override uses the default.
    page.navigateTo(year);
    QCOMPARE(page.viewMode(), ViewMode::Column);

    page.navigateTo(photos);
    QCOMPARE(page.viewMode(), ViewMode::Gallery);
}

void TestSelection::testChangingViewInFolderRecordsOverride() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString docs = root.filePath(QStringLiteral("docs"));
    QVERIFY(QDir().mkpath(docs));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    settings.setDirectViewModeScope(ViewModeScope::CurrentFolder);

    TabPage page(docs);
    page.setViewMode(ViewMode::Gallery);

    QCOMPARE(settings.folderViewMode(docs).value_or(ViewMode::Icon), ViewMode::Gallery);
    // ...and the global default must be untouched.
    QCOMPARE(settings.viewMode(), ViewMode::Column);
}

void TestSelection::testChangingViewWithGlobalScope() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString docs = root.filePath(QStringLiteral("docs"));
    const QString photos = root.filePath(QStringLiteral("photos"));
    QVERIFY(QDir().mkpath(docs));
    QVERIFY(QDir().mkpath(photos));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    settings.setDirectViewModeScope(ViewModeScope::AllFolders);

    TabPage page(docs);
    page.setViewMode(ViewMode::List);

    QCOMPARE(settings.viewMode(), ViewMode::List);
    QVERIFY(!settings.hasFolderViewModeOverride(docs));

    // Another folder without an override follows the new default.
    page.navigateTo(photos);
    QCOMPARE(page.viewMode(), ViewMode::List);
}

void TestSelection::testVirtualViewSwitchDoesNotPersist() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString photos = root.filePath(QStringLiteral("photos"));
    QVERIFY(QDir().mkpath(photos));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    // Column view is the case that forces a switch to List for flat collections.
    settings.setFolderViewMode(photos, ViewMode::Column);

    TabPage page(photos);
    QCOMPARE(page.viewMode(), ViewMode::Column);

    // A virtual collection is shown as a list, but that must not overwrite the
    // real folder's override (nor create a tag:// entry).
    page.showTaggedFiles(QStringLiteral("SomeTag"));
    QCOMPARE(page.viewMode(), ViewMode::List);
    QCOMPARE(settings.folderViewMode(photos).value_or(ViewMode::Icon), ViewMode::Column);
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("tag://SomeTag")));

    // Returning shows the folder's own mode again.
    page.navigateTo(photos);
    QCOMPARE(page.viewMode(), ViewMode::Column);
}

void TestSelection::testClearOverrideFallsBackImmediately() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString photos = root.filePath(QStringLiteral("photos"));
    QVERIFY(QDir().mkpath(photos));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    settings.setFolderViewMode(photos, ViewMode::Gallery);

    TabPage page(photos);
    QCOMPARE(page.viewMode(), ViewMode::Gallery);
    QVERIFY(page.hasFolderViewModeOverride());

    page.clearFolderViewModeOverride();
    QCOMPARE(page.viewMode(), ViewMode::Column);
    QVERIFY(!page.hasFolderViewModeOverride());
}

void TestSelection::testSplitPanesKeepIndependentModes() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString photos = root.filePath(QStringLiteral("photos"));
    const QString docs = root.filePath(QStringLiteral("docs"));
    QVERIFY(QDir().mkpath(photos));
    QVERIFY(QDir().mkpath(docs));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);
    settings.setFolderViewMode(photos, ViewMode::Gallery);

    SplitTabWidget split(photos);
    split.toggleSplit();
    QVERIFY(split.isSplit());
    QVERIFY(split.primaryPage());
    QVERIFY(split.secondaryPage());

    split.secondaryPage()->navigateTo(docs);

    // Each pane resolves its own folder; they must not influence each other.
    QCOMPARE(split.primaryPage()->viewMode(), ViewMode::Gallery);
    QCOMPARE(split.secondaryPage()->viewMode(), ViewMode::Column);
}

void TestSelection::testSessionRestoreDoesNotPersist() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString docs = root.filePath(QStringLiteral("docs"));
    QVERIFY(QDir().mkpath(docs));

    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewModesEnabled(true);

    TabPage page(docs);
    // Session restore applies a stored per-tab mode; it must not be recorded as
    // a user choice, or every launch would rewrite the settings.
    page.applyViewMode(ViewMode::Gallery);

    QCOMPARE(page.viewMode(), ViewMode::Gallery);
    QVERIFY(!settings.hasFolderViewModeOverride(docs));
    QCOMPARE(settings.viewMode(), ViewMode::Column);
}

void TestSelection::testAppChooserDialog() {
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
