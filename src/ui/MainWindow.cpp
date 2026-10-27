#include "MainWindow.h"
#include "ArchiveService.h"
#include "TagManager.h"
#include "AppSettings.h"
#include "ConflictDialog.h"
#include "GetInfoDialog.h"
#include "ThemeManager.h"
#include "VolumeManager.h"
#include "XdgTrash.h"
#include "DesktopAppsManager.h"
#include "AppChooserDialog.h"
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMimeDatabase>
#include <QDesktopServices>
#include <QUrl>
#include <QStorageInfo>
#include <QWidgetAction>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_opEngine = new FileOperationEngine(this);
    m_undoManager = new UndoManager(this);
    m_progressDialog = new ProgressDialog(this);
    m_quickLookDialog = new QuickLookDialog(this);

    connect(m_opEngine, &FileOperationEngine::operationStarted, this, &MainWindow::onOperationStarted);
    connect(m_opEngine, &FileOperationEngine::progress, this, &MainWindow::onOperationProgress);
    connect(m_opEngine, &FileOperationEngine::conflictEncountered, this, &MainWindow::onOperationConflict);
    connect(m_opEngine, &FileOperationEngine::operationFinished, this, &MainWindow::onOperationFinished);
    connect(m_progressDialog, &ProgressDialog::cancelRequested, m_opEngine, &FileOperationEngine::cancel);

    setupUi();
    setupMenusAndShortcuts();

    // Restore geometry
    if (!AppSettings::instance().windowGeometry().isEmpty()) {
        restoreGeometry(AppSettings::instance().windowGeometry());
    } else {
        resize(1024, 680);
    }

    // Initial navigation
    QString startPath = AppSettings::instance().defaultPath();
    openPath(startPath);
}

MainWindow::~MainWindow()
{
    AppSettings::instance().setWindowGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());
    AppSettings::instance().save();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    AppSettings::instance().setWindowGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());
    AppSettings::instance().save();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("Finder"));

    // Toolbar
    m_toolbar = new Toolbar(this);
    addToolBar(Qt::TopToolBarArea, m_toolbar);

    connect(m_toolbar, &Toolbar::backRequested, this, &MainWindow::onNavigateBack);
    connect(m_toolbar, &Toolbar::forwardRequested, this, &MainWindow::onNavigateForward);
    connect(m_toolbar, &Toolbar::pathChanged, this, &MainWindow::onPathChanged);
    connect(m_toolbar, &Toolbar::viewModeChanged, this, &MainWindow::onViewModeChanged);
    connect(m_toolbar, &Toolbar::newFolderRequested, this, &MainWindow::onNewFolder);
    connect(m_toolbar, &Toolbar::quickLookRequested, this, &MainWindow::onQuickLook);
    connect(m_toolbar, &Toolbar::getInfoRequested, this, &MainWindow::onGetInfo);
    connect(m_toolbar, &Toolbar::toggleHiddenRequested, this, &MainWindow::onToggleHidden);
    connect(m_toolbar, &Toolbar::openTerminalRequested, this, &MainWindow::onOpenTerminal);
    connect(m_toolbar, &Toolbar::searchTextChanged, this, &MainWindow::onSearchTextChanged);

    // Splitter: Sidebar + TabManager
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(1);

    m_sidebar = new Sidebar(m_mainSplitter);
    m_sidebar->setFixedWidth(210);
    connect(m_sidebar, &Sidebar::pathSelected, this, &MainWindow::onPathChanged);
    connect(m_sidebar, &Sidebar::openInNewTabRequested, this, [this](const QString& p) {
        m_tabManager->createTab(p);
    });
    connect(m_sidebar, &Sidebar::getInfoRequested, this, [this](const QString& p) {
        auto* dlg = new GetInfoDialog(p, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    connect(m_sidebar, &Sidebar::emptyTrashRequested, this, &MainWindow::onEmptyTrash);
    connect(m_sidebar, &Sidebar::volumeUnmounted, this, [this](const QString& p) {
        auto* page = m_tabManager->currentPage();
        if (page) {
            if (page->currentPath().startsWith(p)) {
                page->navigateTo(QDir::homePath());
            } else {
                page->refresh();
            }
        }
    });
    connect(m_sidebar, &Sidebar::itemsDroppedOnPath, this, &MainWindow::onFilesDropped);
    connect(m_sidebar, &Sidebar::itemsDroppedOnTrash, this, [this](const QStringList& sources) {
        m_opEngine->trashItems(sources);
    });
    connect(m_sidebar, &Sidebar::tagSelected, this, [this](const QString& tag) {
        auto* page = m_tabManager->currentPage();
        if (page) {
            page->model()->setTaggedFiles(tag);
            m_toolbar->setPath(QStringLiteral("tag://") + tag);
            updateNavigationState();
            updateStatusInfo();
        }
    });

    m_tabManager = new TabManager(m_mainSplitter);
    connect(m_tabManager, &TabManager::activePageChanged, this, &MainWindow::onActivePageChanged);
    connect(m_tabManager, &TabManager::quickLookRequested, this, [this](const QString& p) {
        m_quickLookDialog->showFile(p);
        m_quickLookDialog->show();
        m_quickLookDialog->raise();
        m_quickLookDialog->activateWindow();
    });
    connect(m_tabManager, &TabManager::getInfoRequested, this, [this](const QString& p) {
        auto* dlg = new GetInfoDialog(p, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    connect(m_tabManager, &TabManager::contextMenuRequested, this, &MainWindow::onContextMenuRequested);
    connect(m_tabManager, &TabManager::fileDropRequested, this, &MainWindow::onFilesDropped);
    connect(m_tabManager, &TabManager::searchClearRequested, this, [this]() {
        m_toolbar->clearSearch();
    });

    m_mainSplitter->addWidget(m_sidebar);
    m_mainSplitter->addWidget(m_tabManager);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);

    setCentralWidget(m_mainSplitter);

    // Status bar
    auto* sBar = statusBar();
    m_statusItemCountLabel = new QLabel(this);
    m_statusDiskFreeLabel = new QLabel(this);

    sBar->addWidget(m_statusItemCountLabel);
    sBar->addPermanentWidget(m_statusDiskFreeLabel);
}

void MainWindow::setupMenusAndShortcuts()
{
    auto* mBar = menuBar();

    // File Menu
    auto* fileMenu = mBar->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("New Tab"), QKeySequence(Qt::CTRL | Qt::Key_T), this, [this]() {
        TabPage* curr = m_tabManager->currentPage();
        QString path = curr ? curr->currentPath() : AppSettings::instance().defaultPath();
        m_tabManager->createTab(path);
    });

    fileMenu->addAction(QStringLiteral("New Folder"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this, &MainWindow::onNewFolder);
    fileMenu->addAction(QStringLiteral("New Text File..."), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), this, &MainWindow::onNewTextFile);
    fileMenu->addAction(QStringLiteral("Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this]() {
        m_tabManager->closeCurrentTab();
    });

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Get Info"), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::onGetInfo);
    fileMenu->addAction(QStringLiteral("Quick Look"), QKeySequence(Qt::Key_Space), this, &MainWindow::onQuickLook);
    fileMenu->addAction(QStringLiteral("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::onDuplicate);
    fileMenu->addAction(QStringLiteral("Compress"), this, [this]() { onCompress(QStringLiteral("zip")); });
    fileMenu->addAction(QStringLiteral("Extract Here"), this, &MainWindow::onExtractHere);
    fileMenu->addAction(QStringLiteral("Make Link / Alias"), this, &MainWindow::onMakeSymlink);
    fileMenu->addAction(QStringLiteral("Copy Path"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this, &MainWindow::onCopyPath);

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Move to Trash"), QKeySequence(Qt::Key_Delete), this, &MainWindow::onMoveToTrash);
    fileMenu->addAction(QStringLiteral("Delete Permanently"), QKeySequence(Qt::SHIFT | Qt::Key_Delete), this, &MainWindow::onDeletePermanently);
    fileMenu->addAction(QStringLiteral("Put Back"), this, &MainWindow::onPutBack);
    fileMenu->addAction(QStringLiteral("Empty Trash..."), this, &MainWindow::onEmptyTrash);

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), QKeySequence(Qt::CTRL | Qt::Key_Q), this, &QWidget::close);

    // Edit Menu
    auto* editMenu = mBar->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("Undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), this, &MainWindow::onUndo);
    editMenu->addAction(QStringLiteral("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this, &MainWindow::onRedo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Cut"), QKeySequence(Qt::CTRL | Qt::Key_X), this, &MainWindow::onCut);
    editMenu->addAction(QStringLiteral("Copy"), QKeySequence(Qt::CTRL | Qt::Key_C), this, &MainWindow::onCopy);
    editMenu->addAction(QStringLiteral("Paste"), QKeySequence(Qt::CTRL | Qt::Key_V), this, &MainWindow::onPaste);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Rename"), QKeySequence(Qt::Key_F2), this, &MainWindow::onRename);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Find..."), QKeySequence::Find, this, [this]() {
        m_toolbar->focusSearch();
    });

    // View Menu
    auto* viewMenu = mBar->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("as Icons"), QKeySequence(Qt::CTRL | Qt::Key_1), this, [this]() { onViewModeChanged(ViewMode::Icon); });
    viewMenu->addAction(QStringLiteral("as List"), QKeySequence(Qt::CTRL | Qt::Key_2), this, [this]() { onViewModeChanged(ViewMode::List); });
    viewMenu->addAction(QStringLiteral("as Columns"), QKeySequence(Qt::CTRL | Qt::Key_3), this, [this]() { onViewModeChanged(ViewMode::Column); });
    viewMenu->addAction(QStringLiteral("as Gallery"), QKeySequence(Qt::CTRL | Qt::Key_4), this, [this]() { onViewModeChanged(ViewMode::Gallery); });
    viewMenu->addSeparator();
    m_toggleHiddenAction = viewMenu->addAction(QStringLiteral("Toggle Hidden Files"), QKeySequence(Qt::CTRL | Qt::Key_H), this, &MainWindow::onToggleHidden);
    m_toggleHiddenAction->setCheckable(true);
    m_toggleHiddenAction->setChecked(AppSettings::instance().showHiddenFiles());
    viewMenu->addAction(QStringLiteral("Customize Sidebar..."), m_sidebar, &Sidebar::openManageSidebarDialog);

    // Go Menu
    auto* goMenu = mBar->addMenu(QStringLiteral("&Go"));
    goMenu->addAction(QStringLiteral("Back"), QKeySequence(Qt::ALT | Qt::Key_Left), this, &MainWindow::onNavigateBack);
    goMenu->addAction(QStringLiteral("Forward"), QKeySequence(Qt::ALT | Qt::Key_Right), this, &MainWindow::onNavigateForward);
    goMenu->addAction(QStringLiteral("Enclosing Folder"), QKeySequence(Qt::ALT | Qt::Key_Up), this, &MainWindow::onNavigateUp);
    goMenu->addSeparator();
    goMenu->addAction(QStringLiteral("Go to Folder..."), QKeySequence(Qt::CTRL | Qt::Key_L), this, [this]() {
        m_toolbar->breadcrumbBar()->showPathEditor();
    });
    goMenu->addAction(QStringLiteral("Home"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this, [this]() { openPath(QDir::homePath()); });

    // Theme Menu
    auto* themeMenu = mBar->addMenu(QStringLiteral("&Theme"));
    themeMenu->addAction(QStringLiteral("Light"), this, [this]() {
        AppSettings::instance().setTheme(AppTheme::Light);
        ThemeManager::instance().applyTheme(AppTheme::Light);
        if (auto* p = m_tabManager->currentPage()) p->refresh();
        m_sidebar->refresh();
    });
    themeMenu->addAction(QStringLiteral("Dark"), this, [this]() {
        AppSettings::instance().setTheme(AppTheme::Dark);
        ThemeManager::instance().applyTheme(AppTheme::Dark);
        if (auto* p = m_tabManager->currentPage()) p->refresh();
        m_sidebar->refresh();
    });
    themeMenu->addAction(QStringLiteral("Follow System"), this, [this]() {
        AppSettings::instance().setTheme(AppTheme::System);
        ThemeManager::instance().applyTheme(AppTheme::System);
        if (auto* p = m_tabManager->currentPage()) p->refresh();
        m_sidebar->refresh();
    });
}

void MainWindow::openPath(const QString& path)
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) {
        m_tabManager->createTab(path);
    } else {
        page->navigateTo(path);
    }
}

void MainWindow::onNavigateBack()
{
    if (TabPage* page = m_tabManager->currentPage()) {
        page->goBack();
    }
}

void MainWindow::onNavigateForward()
{
    if (TabPage* page = m_tabManager->currentPage()) {
        page->goForward();
    }
}

void MainWindow::onNavigateUp()
{
    if (TabPage* page = m_tabManager->currentPage()) {
        page->goUp();
    }
}

void MainWindow::onPathChanged(const QString& path)
{
    openPath(path);
}

void MainWindow::onViewModeChanged(ViewMode mode)
{
    AppSettings::instance().setViewMode(mode);
    m_toolbar->setViewMode(mode);
    if (TabPage* page = m_tabManager->currentPage()) {
        page->setViewMode(mode);
    }
}

void MainWindow::onActivePageChanged(TabPage* page)
{
    if (!page) return;

    m_toolbar->setPath(page->currentPath());
    m_toolbar->setViewMode(page->viewMode());
    m_sidebar->selectPath(page->currentPath());

    connect(page, &TabPage::statusMessageChanged, this, [this](const QString& msg) {
        m_statusItemCountLabel->setText(msg);
    });

    updateNavigationState();
    updateStatusInfo();
}

void MainWindow::updateNavigationState()
{
    if (TabPage* page = m_tabManager->currentPage()) {
        m_toolbar->setCanGoBack(page->canGoBack());
        m_toolbar->setCanGoForward(page->canGoForward());
    }
}

void MainWindow::updateStatusInfo()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStorageInfo storage(page->currentPath());
    if (storage.isValid()) {
        m_statusDiskFreeLabel->setText(QStringLiteral("%1 available").arg(FileItem::formatSize(storage.bytesAvailable())));
    }
}

void MainWindow::onNewFolder()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("New Folder"),
                                         QStringLiteral("Folder name:"),
                                         QLineEdit::Normal,
                                         QStringLiteral("untitled folder"), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        QString createdPath, error;
        if (m_opEngine->createFolder(page->currentPath(), name.trimmed(), &createdPath, &error)) {
            UndoAction act;
            act.type = UndoActionType::Create;
            act.description = QStringLiteral("Create \"%1\"").arg(name.trimmed());
            act.targetPath = createdPath;
            m_undoManager->pushAction(act);
            page->refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"), error);
        }
    }
}

void MainWindow::onNewTextFile()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("New File"),
                                         QStringLiteral("File name:"),
                                         QLineEdit::Normal,
                                         QStringLiteral("untitled.txt"), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        QString createdPath, error;
        if (m_opEngine->createFile(page->currentPath(), name.trimmed(), &createdPath, &error)) {
            UndoAction act;
            act.type = UndoActionType::Create;
            act.description = QStringLiteral("Create \"%1\"").arg(name.trimmed());
            act.targetPath = createdPath;
            m_undoManager->pushAction(act);
            page->refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"), error);
        }
    }
}

void MainWindow::onMakeSymlink()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty()) paths.append(single);
    }

    for (const auto& p : paths) {
        QFileInfo fi(p);
        QString linkPath = fi.dir().filePath(fi.fileName() + QStringLiteral(" link"));
        m_opEngine->createSymlink(p, linkPath);
    }
    page->refresh();
}

void MainWindow::onCopyPath()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty()) paths.append(single);
    }
    if (!paths.isEmpty()) {
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
    }
}

void MainWindow::onRename()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QString selected = page->selectedFilePath();
    if (selected.isEmpty()) return;

    QFileInfo fi(selected);
    bool ok = false;
    QString newName = QInputDialog::getText(this, QStringLiteral("Rename"),
                                            QStringLiteral("New name:"),
                                            QLineEdit::Normal,
                                            fi.fileName(), &ok);
    if (ok && !newName.trimmed().isEmpty() && newName.trimmed() != fi.fileName()) {
        QString renamedPath, error;
        if (m_opEngine->renameItem(selected, newName.trimmed(), &renamedPath, &error)) {
            UndoAction act;
            act.type = UndoActionType::Rename;
            act.description = QStringLiteral("Rename \"%1\" to \"%2\"").arg(fi.fileName(), newName.trimmed());
            act.sourcePath = selected;
            act.targetPath = renamedPath;
            m_undoManager->pushAction(act);
            page->refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"), error);
        }
    }
}

void MainWindow::onCopy()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;
    m_clipboardPaths = page->selectedFilePaths();
    m_isCutOperation = false;
}

void MainWindow::onCut()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;
    m_clipboardPaths = page->selectedFilePaths();
    m_isCutOperation = true;
}

void MainWindow::onPaste()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page || m_clipboardPaths.isEmpty()) return;

    QString destDir = page->currentPath();
    if (m_isCutOperation) {
        m_opEngine->moveItems(m_clipboardPaths, destDir);
        m_clipboardPaths.clear();
        m_isCutOperation = false;
    } else {
        m_opEngine->copyItems(m_clipboardPaths, destDir);
    }
}

void MainWindow::onDuplicate()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;
    QStringList paths = page->selectedFilePaths();
    if (!paths.isEmpty()) {
        m_opEngine->duplicateItems(paths);
    }
}

void MainWindow::onCompress(const QString& ext)
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;
    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) return;

    QString format = ext.isEmpty() ? QStringLiteral("zip") : ext;
    QString destArchive = ArchiveService::suggestedArchiveName(paths, format);
    m_opEngine->compressItems(paths, destArchive);
}

void MainWindow::onExtractHere()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;
    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) return;

    for (const auto& p : paths) {
        if (ArchiveService::isArchive(p)) {
            QString destDir = QFileInfo(p).absolutePath();
            m_opEngine->extractArchive(p, destDir);
        }
    }
}

void MainWindow::onExtractToFolder()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;
    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) return;

    for (const auto& p : paths) {
        if (ArchiveService::isArchive(p)) {
            QString destDir = ArchiveService::suggestedExtractDir(p);
            m_opEngine->extractArchive(p, destDir);
        }
    }
}

void MainWindow::onMoveToTrash()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) return;

    m_opEngine->trashItems(paths);
}

void MainWindow::onDeletePermanently()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) return;

    auto btn = QMessageBox::warning(this, QStringLiteral("Permanent Delete"),
        QStringLiteral("Are you sure you want to permanently delete these %1 items?\nThis action cannot be undone.").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn == QMessageBox::Yes) {
        m_opEngine->deletePermanently(paths);
    }
}

void MainWindow::onEmptyTrash()
{
    auto btn = QMessageBox::warning(this, QStringLiteral("Empty Trash"),
        QStringLiteral("Are you sure you want to permanently erase the items in the Trash?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn == QMessageBox::Yes) {
        QString error;
        if (!XdgTrash::emptyTrash(&error)) {
            QMessageBox::warning(this, QStringLiteral("Empty Trash Error"), error);
        }
        if (TabPage* page = m_tabManager->currentPage()) {
            page->refresh();
        }
        m_sidebar->refresh();
    }
}

void MainWindow::onPutBack()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty()) paths.append(single);
    }
    if (paths.isEmpty()) return;

    int failed = 0;
    QString lastErr;
    for (const auto& p : paths) {
        QString err;
        if (!XdgTrash::restoreItem(p, &err)) {
            failed++;
            lastErr = err;
        }
    }

    if (failed > 0) {
        QMessageBox::warning(this, QStringLiteral("Restore Error"),
            failed == 1 ? lastErr : QStringLiteral("Failed to restore %1 items: %2").arg(failed).arg(lastErr));
    }

    page->refresh();
    m_sidebar->refresh();
}

void MainWindow::onFilesDropped(const QStringList& sources, const QString& targetDir, Qt::DropAction action)
{
    if (sources.isEmpty() || targetDir.isEmpty()) return;

    // Check if target is trash
    if (targetDir == XdgTrash::trashFilesDirectory() || targetDir.startsWith(XdgTrash::trashDirectory())) {
        m_opEngine->trashItems(sources);
        return;
    }

    if (action == Qt::CopyAction) {
        m_opEngine->copyItems(sources, targetDir);
    } else {
        m_opEngine->moveItems(sources, targetDir);
    }
}

void MainWindow::onQuickLook()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty()) paths.append(single);
    }

    if (!paths.isEmpty()) {
        m_quickLookDialog->setFiles(paths);
        m_quickLookDialog->show();
        m_quickLookDialog->raise();
        m_quickLookDialog->activateWindow();
    }
}

void MainWindow::onGetInfo()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QString path = page->selectedFilePath();
    if (path.isEmpty()) {
        path = page->currentPath();
    }

    auto* dlg = new GetInfoDialog(path, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onToggleHidden()
{
    bool show = !AppSettings::instance().showHiddenFiles();
    AppSettings::instance().setShowHiddenFiles(show);
    m_toolbar->setShowHidden(show);
    if (m_toggleHiddenAction) {
        m_toggleHiddenAction->setChecked(show);
    }
    m_tabManager->setShowHidden(show);
}

void MainWindow::onOpenTerminal()
{
    TabPage* page = m_tabManager->currentPage();
    if (!page) return;

    QString currentDir = page->currentPath();
    // Try launching installed terminal
    QStringList terms = {
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("ptyxis"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("konsole"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("kitty"),
        QStringLiteral("alacritty"),
        QStringLiteral("xterm")
    };

    bool started = false;
    for (const auto& term : terms) {
        if (QProcess::startDetached(term, QStringList(), currentDir)) {
            started = true;
            break;
        }
    }

    if (!started) {
        QMessageBox::information(this, QStringLiteral("Terminal"),
            QStringLiteral("Could not find a terminal emulator installed."));
    }
}

void MainWindow::onUndo()
{
    if (m_undoManager->canUndo()) {
        QString error;
        if (!m_undoManager->undo(&error)) {
            QMessageBox::warning(this, QStringLiteral("Undo Failed"), error);
        }
        if (TabPage* page = m_tabManager->currentPage()) {
            page->refresh();
        }
    }
}

void MainWindow::onRedo()
{
    if (m_undoManager->canRedo()) {
        QString error;
        if (!m_undoManager->redo(&error)) {
            QMessageBox::warning(this, QStringLiteral("Redo Failed"), error);
        }
        if (TabPage* page = m_tabManager->currentPage()) {
            page->refresh();
        }
    }
}

void MainWindow::onSearchTextChanged(const QString& query)
{
    if (TabPage* page = m_tabManager->currentPage()) {
        page->setFilterQuery(query);
    }
}

void MainWindow::onOperationStarted(OperationType type, int totalItems, qint64 totalBytes)
{
    m_progressDialog->setOperation(type, totalItems, totalBytes);
}

void MainWindow::onOperationProgress(const OperationProgress& progress)
{
    m_progressDialog->updateProgress(progress);
}

void MainWindow::onOperationConflict(const OperationConflict& conflict, ConflictResolution& res, bool& applyToAll)
{
    ConflictDialog dlg(conflict, this);
    if (dlg.exec() == QDialog::Accepted) {
        res = dlg.resolution();
        applyToAll = dlg.applyToAll();
    } else {
        res = ConflictResolution::Cancel;
        applyToAll = false;
    }
}

void MainWindow::onOperationFinished(const OperationResult& result)
{
    m_progressDialog->hide();
    QString pathToSelect;
    if (!result.createdPaths.isEmpty()) {
        pathToSelect = result.createdPaths.first();
    }
    if (TabPage* page = m_tabManager->currentPage()) {
        if (!QDir(page->currentPath()).exists()) {
            page->goUp();
        } else {
            page->refresh(pathToSelect);
        }
    }
    m_sidebar->refresh();
    if (TabPage* page = m_tabManager->currentPage()) {
        m_sidebar->selectPath(page->currentPath());
    }
    updateStatusInfo();

    if (!result.success && !result.errorMessage.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Operation Incomplete"), result.errorMessage);
    }
}

void MainWindow::onContextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths, const QString& currentDir)
{
    QMenu menu(this);
    bool inTrash = (currentDir == XdgTrash::trashFilesDirectory() ||
                    currentDir.startsWith(XdgTrash::trashDirectory()) ||
                    (!selectedPaths.isEmpty() && selectedPaths.first().startsWith(XdgTrash::trashFilesDirectory())));

    if (!selectedPaths.isEmpty()) {
        // --- SELECTION CONTEXT MENU ---
        QString primaryPath = selectedPaths.first();
        QFileInfo primaryFi(primaryPath);
        bool isDir = primaryFi.isDir();
        bool single = (selectedPaths.size() == 1);

        if (inTrash) {
            auto* restoreAct = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-undo")),
                single ? QStringLiteral("Put Back")
                       : QStringLiteral("Put Back %1 Items").arg(selectedPaths.size()),
                this, &MainWindow::onPutBack);
            QFont rf = restoreAct->font();
            rf.setBold(true);
            restoreAct->setFont(rf);

            menu.addSeparator();
            menu.addAction(QStringLiteral("Quick Look"), this, &MainWindow::onQuickLook);

            if (single) {
                menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), QStringLiteral("Get Info"), this, [this, primaryPath]() {
                    auto* dlg = new GetInfoDialog(primaryPath, this);
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->show();
                });
            }

            menu.addAction(QStringLiteral("Copy Path"), this, &MainWindow::onCopyPath);
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete Immediately..."), this, &MainWindow::onDeletePermanently);
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("trash-empty")), QStringLiteral("Empty Trash..."), this, &MainWindow::onEmptyTrash);

            menu.exec(globalPos);
            return;
        }

        // 1. Open
        if (single) {
            auto* openAct = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this, [this, primaryPath, isDir]() {
                if (isDir) {
                    openPath(primaryPath);
                } else {
                    DesktopAppsManager::launchFile(primaryPath);
                }
            });
            QFont f = openAct->font();
            f.setBold(true);
            openAct->setFont(f);

            if (isDir) {
                menu.addAction(QStringLiteral("Open in New Tab"), this, [this, primaryPath]() {
                    m_tabManager->createTab(primaryPath);
                });

                bool isMount = VolumeManager::isMountPoint(primaryPath);
                if (isMount && primaryPath != QStringLiteral("/")) {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("media-eject")), QStringLiteral("Eject / Unmount"), this, [this, primaryPath]() {
                        QString err;
                        if (VolumeManager::unmountVolume(primaryPath, &err)) {
                            m_sidebar->refresh();
                            auto* page = m_tabManager->currentPage();
                            if (page) {
                                if (page->currentPath().startsWith(primaryPath)) {
                                    page->navigateTo(QDir::homePath());
                                } else {
                                    page->refresh();
                                }
                            }
                        } else {
                            QMessageBox::warning(this, QStringLiteral("Cannot Unmount"), err);
                        }
                    });
                }

                bool isPinned = AppSettings::instance().customFavorites().contains(primaryPath);
                if (isPinned) {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-remove")), QStringLiteral("Unpin from Sidebar"), this, [this, primaryPath]() {
                        AppSettings::instance().removeFavorite(primaryPath);
                        m_sidebar->refresh();
                    });
                } else {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), QStringLiteral("Pin to Sidebar"), this, [this, primaryPath]() {
                        AppSettings::instance().addFavorite(primaryPath);
                        m_sidebar->refresh();
                    });
                }
            } else {
                // Open With submenu
                QMimeDatabase mimeDb;
                QString mime = mimeDb.mimeTypeForFile(primaryPath).name();
                auto apps = DesktopAppsManager::appsForMime(mime);

                auto* openWithMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("application-x-executable")), QStringLiteral("Open With"));
                if (!apps.isEmpty()) {
                    for (const auto& app : apps) {
                        QString title = app.name;
                        if (app.isDefault) {
                            title += QStringLiteral(" (default)");
                        }
                        openWithMenu->addAction(app.icon, title, this, [appId = app.id, primaryPath]() {
                            DesktopAppsManager::launchApp(appId, primaryPath);
                        });
                    }
                    openWithMenu->addSeparator();
                }

                openWithMenu->addAction(QStringLiteral("Other Application..."), this, [this, mime, primaryFi, primaryPath]() {
                    AppChooserDialog dlg(mime, primaryFi.fileName(), this);
                    if (dlg.exec() == QDialog::Accepted) {
                        QString chosen = dlg.selectedAppId();
                        if (!chosen.isEmpty()) {
                            DesktopAppsManager::launchApp(chosen, primaryPath);
                            if (dlg.makeDefault()) {
                                DesktopAppsManager::setDefaultAppForMime(mime, chosen);
                            }
                        }
                    }
                });
            }
        } else {
            // Multiple items
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open %1 Items").arg(selectedPaths.size()), this, [this, selectedPaths]() {
                for (const auto& p : selectedPaths) {
                    if (QFileInfo(p).isDir()) {
                        openPath(p);
                    } else {
                        DesktopAppsManager::launchFile(p);
                    }
                }
            });
        }

        // Quick Look
        menu.addAction(QStringLiteral("Quick Look"), this, &MainWindow::onQuickLook);

        menu.addSeparator();

        // Get Info
        menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), QStringLiteral("Get Info"), this, [this, primaryPath]() {
            auto* dlg = new GetInfoDialog(primaryPath, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });

        // Tags Bar Widget
        auto* tagWidgetAction = new QWidgetAction(&menu);
        auto* tagContainer = new QWidget(&menu);
        auto* tagRow = new QHBoxLayout(tagContainer);
        tagRow->setContentsMargins(14, 4, 14, 4);
        tagRow->setSpacing(8);

        auto* tagLabel = new QLabel(QStringLiteral("Tags:"), tagContainer);
        tagLabel->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
        tagRow->addWidget(tagLabel);

        for (const auto& t : TagManager::standardTags()) {
            auto* dotBtn = new QToolButton(tagContainer);
            dotBtn->setFixedSize(18, 18);
            dotBtn->setCursor(Qt::PointingHandCursor);
            dotBtn->setToolTip(t.name);

            bool allHave = true;
            for (const auto& sp : selectedPaths) {
                if (!TagManager::instance().hasTag(sp, t.name)) {
                    allHave = false;
                    break;
                }
            }

            dotBtn->setStyleSheet(QStringLiteral(
                "QToolButton {"
                "  background-color: %1;"
                "  border-radius: 9px;"
                "  border: %2;"
                "}"
                "QToolButton:hover { opacity: 0.85; }"
            ).arg(t.color.name(), allHave ? QStringLiteral("2px solid #ffffff") : QStringLiteral("1px solid rgba(0,0,0,0.25)")));

            QObject::connect(dotBtn, &QToolButton::clicked, &menu, [&menu, selectedPaths, tagName = t.name]() {
                for (const auto& sp : selectedPaths) {
                    TagManager::instance().toggleTag(sp, tagName);
                }
                menu.close();
            });
            tagRow->addWidget(dotBtn);
        }

        // Clear tags button
        auto* clearDotBtn = new QToolButton(tagContainer);
        clearDotBtn->setFixedSize(18, 18);
        clearDotBtn->setText(QStringLiteral("✕"));
        clearDotBtn->setCursor(Qt::PointingHandCursor);
        clearDotBtn->setToolTip(QStringLiteral("Remove All Tags"));
        clearDotBtn->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  background-color: rgba(128, 128, 128, 0.25);"
            "  color: #888888;"
            "  border-radius: 9px;"
            "  border: 1px solid rgba(128, 128, 128, 0.4);"
            "  font-size: 10px;"
            "  font-weight: bold;"
            "}"
            "QToolButton:hover { background-color: rgba(128, 128, 128, 0.45); color: #ffffff; }"
        ));
        QObject::connect(clearDotBtn, &QToolButton::clicked, &menu, [&menu, selectedPaths]() {
            for (const auto& sp : selectedPaths) {
                TagManager::instance().clearTags(sp);
            }
            menu.close();
        });
        tagRow->addWidget(clearDotBtn);
        tagRow->addStretch();

        tagWidgetAction->setDefaultWidget(tagContainer);
        menu.addAction(tagWidgetAction);

        // Tags submenu for keyboard accessibility
        auto* tagsMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("tag")), QStringLiteral("Tags"));
        for (const auto& t : TagManager::standardTags()) {
            bool hasTag = false;
            for (const auto& sp : selectedPaths) {
                if (TagManager::instance().hasTag(sp, t.name)) {
                    hasTag = true;
                    break;
                }
            }
            auto* act = tagsMenu->addAction(TagManager::tagDotIcon(t.color, 14), t.name, this, [selectedPaths, tagName = t.name]() {
                for (const auto& sp : selectedPaths) {
                    TagManager::instance().toggleTag(sp, tagName);
                }
            });
            act->setCheckable(true);
            act->setChecked(hasTag);
        }
        tagsMenu->addSeparator();
        tagsMenu->addAction(QStringLiteral("Clear All Tags"), this, [selectedPaths]() {
            for (const auto& sp : selectedPaths) {
                TagManager::instance().clearTags(sp);
            }
        });
        menu.addSeparator();

        bool isMount = isDir && VolumeManager::isMountPoint(primaryPath);

        // Rename (only for single and non-mount)
        if (single && !isMount) {
            menu.addAction(QStringLiteral("Rename"), this, &MainWindow::onRename);
        }

        // Duplicate (non-mount)
        if (!isMount) {
            menu.addAction(QStringLiteral("Duplicate"), this, &MainWindow::onDuplicate);
        }

        // Make Link / Alias
        menu.addAction(QStringLiteral("Make Link / Alias"), this, &MainWindow::onMakeSymlink);

        // Archive Actions (Extract & Compress)
        bool anyArchive = false;
        for (const auto& sp : selectedPaths) {
            if (ArchiveService::isArchive(sp)) {
                anyArchive = true;
                break;
            }
        }

        if (anyArchive) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("package-x-generic")),
                QStringLiteral("Extract Here"), this, &MainWindow::onExtractHere);
            if (single) {
                QString extractFolder = QFileInfo(ArchiveService::suggestedExtractDir(primaryPath)).fileName();
                menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")),
                    QStringLiteral("Extract to \"%1/\"").arg(extractFolder), this, &MainWindow::onExtractToFolder);
            } else {
                menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")),
                    QStringLiteral("Extract Each to Subfolder"), this, &MainWindow::onExtractToFolder);
            }
        }

        if (!isMount) {
            QString compressTitle = single
                ? QStringLiteral("Compress \"%1\"").arg(primaryFi.fileName())
                : QStringLiteral("Compress %1 Items").arg(selectedPaths.size());
            menu.addAction(QIcon::fromTheme(QStringLiteral("archive-insert")),
                compressTitle, this, [this]() { onCompress(QStringLiteral("zip")); });

            auto* compressAsMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("archive-insert")), QStringLiteral("Compress As"));
            compressAsMenu->addAction(QStringLiteral(".zip (ZIP Archive)"), this, [this]() { onCompress(QStringLiteral("zip")); });
            compressAsMenu->addAction(QStringLiteral(".tar.gz (GZip Tarball)"), this, [this]() { onCompress(QStringLiteral("tar.gz")); });
            compressAsMenu->addAction(QStringLiteral(".tar.xz (XZ Tarball)"), this, [this]() { onCompress(QStringLiteral("tar.xz")); });
            compressAsMenu->addAction(QStringLiteral(".7z (7-Zip Archive)"), this, [this]() { onCompress(QStringLiteral("7z")); });
        }

        // Copy Path
        menu.addAction(QStringLiteral("Copy Path"), this, &MainWindow::onCopyPath);

        menu.addSeparator();

        // Cut
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")), QStringLiteral("Cut"), this, &MainWindow::onCut);

        // Copy
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
            single ? QStringLiteral("Copy \"%1\"").arg(primaryFi.fileName())
                   : QStringLiteral("Copy %1 Items").arg(selectedPaths.size()),
            this, &MainWindow::onCopy);

        // If folder is selected and clipboard has files, allow "Paste into Folder"
        if (single && isDir && !m_clipboardPaths.isEmpty()) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-paste")),
                QStringLiteral("Paste into \"%1\"").arg(primaryFi.fileName()), this, [this, primaryPath]() {
                if (m_isCutOperation) {
                    m_opEngine->moveItems(m_clipboardPaths, primaryPath);
                    m_clipboardPaths.clear();
                    m_isCutOperation = false;
                } else {
                    m_opEngine->copyItems(m_clipboardPaths, primaryPath);
                }
            });
        }

        // Move to Trash and Delete Permanently (suppressed for mounts)
        if (!isMount) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")), QStringLiteral("Move to Trash"), this, &MainWindow::onMoveToTrash);
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete Permanently"), this, &MainWindow::onDeletePermanently);
        }

        // If folder, also offer "Open in Terminal"
        if (single && isDir) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), QStringLiteral("Open in Terminal"), this, [primaryPath]() {
                QStringList terms = {
                    QStringLiteral("x-terminal-emulator"),
                    QStringLiteral("ptyxis"),
                    QStringLiteral("gnome-terminal"),
                    QStringLiteral("konsole"),
                    QStringLiteral("xfce4-terminal"),
                    QStringLiteral("kitty"),
                    QStringLiteral("alacritty"),
                    QStringLiteral("xterm")
                };
                for (const auto& term : terms) {
                    if (QProcess::startDetached(term, QStringList(), primaryPath)) {
                        break;
                    }
                }
            });
        }

    } else {
        // --- BACKGROUND / EMPTY SPACE CONTEXT MENU ---
        if (inTrash) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("trash-empty")), QStringLiteral("Empty Trash..."), this, &MainWindow::onEmptyTrash);
            menu.exec(globalPos);
            return;
        }

        menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), QStringLiteral("New Folder"), this, &MainWindow::onNewFolder);
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-new")), QStringLiteral("New Text File..."), this, &MainWindow::onNewTextFile);

        menu.addSeparator();

        // Get Info for current directory
        menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), QStringLiteral("Get Info"), this, [this, currentDir]() {
            auto* dlg = new GetInfoDialog(currentDir, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });

        // Pin / Unpin current directory to Sidebar
        bool isPinned = AppSettings::instance().customFavorites().contains(currentDir);
        if (isPinned) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-remove")), QStringLiteral("Unpin Current Folder from Sidebar"), this, [this, currentDir]() {
                AppSettings::instance().removeFavorite(currentDir);
                m_sidebar->refresh();
            });
        } else {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), QStringLiteral("Pin Current Folder to Sidebar"), this, [this, currentDir]() {
                AppSettings::instance().addFavorite(currentDir);
                m_sidebar->refresh();
            });
        }

        // Paste
        auto* pasteAct = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-paste")),
            m_clipboardPaths.isEmpty() ? QStringLiteral("Paste")
                                       : QStringLiteral("Paste (%1 Item%2)").arg(m_clipboardPaths.size()).arg(m_clipboardPaths.size() == 1 ? QString() : QStringLiteral("s")),
            this, &MainWindow::onPaste);
        pasteAct->setEnabled(!m_clipboardPaths.isEmpty());

        // Select All
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-select-all")), QStringLiteral("Select All"), this, [this]() {
            if (TabPage* p = m_tabManager->currentPage()) {
                p->selectAll();
            }
        });

        menu.addSeparator();

        // Sort By submenu
        auto* sortMenu = menu.addMenu(QStringLiteral("Sort By"));
        sortMenu->addAction(QStringLiteral("Name"), this, [this]() {
            if (TabPage* p = m_tabManager->currentPage()) p->model()->sort(FileSystemModel::ColName);
        });
        sortMenu->addAction(QStringLiteral("Date Modified"), this, [this]() {
            if (TabPage* p = m_tabManager->currentPage()) p->model()->sort(FileSystemModel::ColDateModified);
        });
        sortMenu->addAction(QStringLiteral("Size"), this, [this]() {
            if (TabPage* p = m_tabManager->currentPage()) p->model()->sort(FileSystemModel::ColSize);
        });
        sortMenu->addAction(QStringLiteral("Kind"), this, [this]() {
            if (TabPage* p = m_tabManager->currentPage()) p->model()->sort(FileSystemModel::ColKind);
        });

        // Show Hidden Files (checkable)
        bool showHidden = AppSettings::instance().showHiddenFiles();
        auto* hiddenAct = menu.addAction(QStringLiteral("Show Hidden Files"), this, &MainWindow::onToggleHidden);
        hiddenAct->setCheckable(true);
        hiddenAct->setChecked(showHidden);

        menu.addSeparator();

        // Open in Terminal
        menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), QStringLiteral("Open in Terminal"), this, [currentDir]() {
            QStringList terms = {
                QStringLiteral("x-terminal-emulator"),
                QStringLiteral("ptyxis"),
                QStringLiteral("gnome-terminal"),
                QStringLiteral("konsole"),
                QStringLiteral("xfce4-terminal"),
                QStringLiteral("kitty"),
                QStringLiteral("alacritty"),
                QStringLiteral("xterm")
            };
            for (const auto& term : terms) {
                if (QProcess::startDetached(term, QStringList(), currentDir)) {
                    break;
                }
            }
        });

        // Refresh
        menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Refresh"), this, [this]() {
            if (TabPage* p = m_tabManager->currentPage()) {
                p->refresh();
            }
        });
    }

    menu.exec(globalPos);
}
