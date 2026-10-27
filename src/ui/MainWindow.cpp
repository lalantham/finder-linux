#include "MainWindow.h"
#include "AppSettings.h"
#include "ConflictDialog.h"
#include "GetInfoDialog.h"
#include "ThemeManager.h"
#include "VolumeManager.h"
#include "XdgTrash.h"
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
#include <QUrl>
#include <QStorageInfo>

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
    connect(m_sidebar, &Sidebar::emptyTrashRequested, this, &MainWindow::onEmptyTrash);

    m_tabManager = new TabManager(m_mainSplitter);
    connect(m_tabManager, &TabManager::activePageChanged, this, &MainWindow::onActivePageChanged);
    connect(m_tabManager, &TabManager::quickLookRequested, this, [this](const QString& p) {
        m_quickLookDialog->showFile(p);
        m_quickLookDialog->show();
    });
    connect(m_tabManager, &TabManager::getInfoRequested, this, [this](const QString& p) {
        auto* dlg = new GetInfoDialog(p, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
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
    fileMenu->addAction(QStringLiteral("Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this]() {
        m_tabManager->closeCurrentTab();
    });

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Get Info"), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::onGetInfo);
    fileMenu->addAction(QStringLiteral("Quick Look"), QKeySequence(Qt::Key_Space), this, &MainWindow::onQuickLook);
    fileMenu->addAction(QStringLiteral("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::onDuplicate);

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Move to Trash"), QKeySequence(Qt::Key_Delete), this, &MainWindow::onMoveToTrash);
    fileMenu->addAction(QStringLiteral("Delete Permanently"), QKeySequence(Qt::SHIFT | Qt::Key_Delete), this, &MainWindow::onDeletePermanently);
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

    // View Menu
    auto* viewMenu = mBar->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("as Icons"), QKeySequence(Qt::CTRL | Qt::Key_1), this, [this]() { onViewModeChanged(ViewMode::Icon); });
    viewMenu->addAction(QStringLiteral("as List"), QKeySequence(Qt::CTRL | Qt::Key_2), this, [this]() { onViewModeChanged(ViewMode::List); });
    viewMenu->addAction(QStringLiteral("as Columns"), QKeySequence(Qt::CTRL | Qt::Key_3), this, [this]() { onViewModeChanged(ViewMode::Column); });
    viewMenu->addAction(QStringLiteral("as Gallery"), QKeySequence(Qt::CTRL | Qt::Key_4), this, [this]() { onViewModeChanged(ViewMode::Gallery); });
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Toggle Hidden Files"), QKeySequence(Qt::CTRL | Qt::Key_H), this, &MainWindow::onToggleHidden);

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
    themeMenu->addAction(QStringLiteral("Light"), this, []() {
        AppSettings::instance().setTheme(AppTheme::Light);
        ThemeManager::instance().applyTheme(AppTheme::Light);
    });
    themeMenu->addAction(QStringLiteral("Dark"), this, []() {
        AppSettings::instance().setTheme(AppTheme::Dark);
        ThemeManager::instance().applyTheme(AppTheme::Dark);
    });
    themeMenu->addAction(QStringLiteral("Follow System"), this, []() {
        AppSettings::instance().setTheme(AppTheme::System);
        ThemeManager::instance().applyTheme(AppTheme::System);
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

    if (TabPage* page = m_tabManager->currentPage()) {
        page->setShowHidden(show);
    }
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
    if (TabPage* page = m_tabManager->currentPage()) {
        page->refresh();
    }
    m_sidebar->refresh();
    updateStatusInfo();

    if (!result.success && !result.errorMessage.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Operation Incomplete"), result.errorMessage);
    }
}
