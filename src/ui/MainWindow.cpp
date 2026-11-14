#include "MainWindow.h"
#include "AppChooserDialog.h"
#include "AppSettings.h"
#include "ApplicationService.h"
#include "ArchiveService.h"
#include "BatchRenameDialog.h"
#include "CompressDialog.h"
#include "ConflictDialog.h"
#include "DesktopAppsManager.h"
#include "DesktopNotifier.h"
#include "DiskImageManager.h"
#include "FileManager1Service.h"
#include "FileMetadataManager.h"
#include "GetInfoDialog.h"
#include "RecentFilesManager.h"
#include "ServiceMenuManager.h"
#include "SmartFolderDialog.h"
#include "TagManager.h"
#include "TerminalPanel.h"
#include "ThemeManager.h"
#include "TrashDialog.h"
#include "ViewOptionsDialog.h"
#include "VolumeManager.h"
#include "XdgTrash.h"
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHash>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_KWINDOWSYSTEM
#include <KWindowSystem>
#endif

namespace {

// Formats understood by GNOME Files, Dolphin and other freedesktop file managers.
constexpr char GnomeClipboardMime[] = "x-special/gnome-copied-files";
constexpr char KdeCutMime[] = "application/x-kde-cutselection";
constexpr char PortalFileTransferMime[] = "application/vnd.portal.filetransfer";

QStringList retrievePortalFiles(const QString& key) {
    if (key.isEmpty()) {
        return {};
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Documents"),
        QStringLiteral("/org/freedesktop/portal/documents"),
        QStringLiteral("org.freedesktop.portal.FileTransfer"), QStringLiteral("RetrieveFiles"));
    message << key << QVariantMap();

    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        return {};
    }

    QStringList paths;
    const QStringList files = reply.arguments().first().toStringList();
    for (const QString& file : files) {
        const QUrl url(file);
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty() && !paths.contains(path)) {
                paths.append(path);
            }
        } else if (file.startsWith(QLatin1Char('/'))) {
            if (!paths.contains(file)) {
                paths.append(file);
            }
        }
    }
    return paths;
}

void setFileClipboard(const QStringList& paths, bool cut) {
    auto* mime = new QMimeData();
    QList<QUrl> urls;
    urls.reserve(paths.size());
    QStringList gnomeLines;
    gnomeLines << (cut ? QStringLiteral("cut") : QStringLiteral("copy"));

    for (const auto& path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        gnomeLines << url.toString();
    }

    mime->setUrls(urls);
    mime->setData(QString::fromLatin1(GnomeClipboardMime),
                  gnomeLines.join(QLatin1Char('\n')).toUtf8());
    mime->setData(QString::fromLatin1(KdeCutMime), cut ? QByteArray("1") : QByteArray("0"));
    QApplication::clipboard()->setMimeData(mime);
}

struct ClipboardFiles {
    QStringList paths;
    bool cut{false};
    bool valid{false};
};

ClipboardFiles readFileClipboard() {
    ClipboardFiles result;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime)
        return result;

    const QByteArray gnome = mime->data(QString::fromLatin1(GnomeClipboardMime));
    if (!gnome.isEmpty()) {
        const QStringList lines =
            QString::fromUtf8(gnome).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            result.cut =
                lines.first().trimmed().compare(QStringLiteral("cut"), Qt::CaseInsensitive) == 0;
            for (int i = 1; i < lines.size(); ++i) {
                const QUrl url(lines.at(i).trimmed());
                if (url.isLocalFile() && !result.paths.contains(url.toLocalFile())) {
                    result.paths.append(url.toLocalFile());
                }
            }
            result.valid = !result.paths.isEmpty();
            if (result.valid)
                return result;
        }
    }

    if (mime->hasUrls()) {
        for (const auto& url : mime->urls()) {
            if (url.isLocalFile() && !result.paths.contains(url.toLocalFile())) {
                result.paths.append(url.toLocalFile());
            }
        }
        result.cut = mime->data(QString::fromLatin1(KdeCutMime)) == QByteArray("1");
        result.valid = !result.paths.isEmpty();
    }

    // Flatpak/sandboxed apps expose copied files through the FileTransfer
    // portal instead of text/uri-list. The clipboard data is the transfer key.
    if (!result.valid) {
        const QByteArray portalData = mime->data(QString::fromLatin1(PortalFileTransferMime));
        if (!portalData.isEmpty()) {
            const QString key = QString::fromUtf8(portalData).trimmed();
            result.paths = retrievePortalFiles(key);
            result.valid = !result.paths.isEmpty();
            result.cut = false;
        }
    }

    return result;
}

QStringList existingPathsOnly(const QStringList& paths) {
    QStringList result;
    for (const auto& path : paths) {
        QFileInfo info(path);
        if (info.exists() || info.isSymLink()) {
            result.append(path);
        }
    }
    return result;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_opEngine = new FileOperationEngine(this);
    m_undoManager = new UndoManager(this);
    m_progressDialog = new ProgressDialog(this);
    m_quickLookDialog = new QuickLookDialog(this);

    connect(m_opEngine, &FileOperationEngine::operationStarted, this,
            &MainWindow::onOperationStarted);
    connect(m_opEngine, &FileOperationEngine::progress, this, &MainWindow::onOperationProgress);
    connect(m_opEngine, &FileOperationEngine::conflictEncountered, this,
            &MainWindow::onOperationConflict);
    connect(m_opEngine, &FileOperationEngine::operationFinished, this,
            &MainWindow::onOperationFinished);
    connect(m_opEngine, &FileOperationEngine::queueStatusChanged, m_progressDialog,
            &ProgressDialog::updateQueueStatus);
    connect(m_progressDialog, &ProgressDialog::cancelRequested, m_opEngine,
            &FileOperationEngine::cancelAll);
    connect(m_progressDialog, &ProgressDialog::skipRequested, m_opEngine,
            &FileOperationEngine::cancel);
    connect(m_progressDialog, &ProgressDialog::skipCurrentFileRequested, m_opEngine,
            &FileOperationEngine::skipCurrentFile);
    connect(m_progressDialog, &ProgressDialog::pauseRequested, this, [this]() {
        m_opEngine->pause();
        m_progressDialog->setPaused(true);
    });
    connect(m_progressDialog, &ProgressDialog::resumeRequested, this, [this]() {
        m_opEngine->resume();
        m_progressDialog->setPaused(false);
    });

    qApp->installEventFilter(this);

    setupUi();
    setupMenusAndShortcuts();

    // Restore geometry and toolbar/dock state before the first frame.
    if (!AppSettings::instance().windowGeometry().isEmpty()) {
        restoreGeometry(AppSettings::instance().windowGeometry());
    } else {
        resize(1024, 680);
    }
    if (!AppSettings::instance().windowState().isEmpty()) {
        restoreState(AppSettings::instance().windowState());
    }
    applyChromeVisibility();

    // Initial navigation
    const QStringList savedTabs = AppSettings::instance().savedTabs();
    if (AppSettings::instance().restoreTabsOnStartup() && !savedTabs.isEmpty()) {
        bool first = true;
        for (const QString& entry : savedTabs) {
            // Entries are "path" (older versions) or "path\tviewMode".
            QString tabPath = entry;
            int viewMode = -1;
            const qsizetype tabIndex = entry.lastIndexOf(QLatin1Char('\t'));
            if (tabIndex > 0) {
                bool ok = false;
                const int parsed = entry.mid(tabIndex + 1).toInt(&ok);
                if (ok) {
                    tabPath = entry.left(tabIndex);
                    viewMode = parsed;
                }
            }

            if (!QDir(tabPath).exists())
                continue;

            if (first) {
                openPath(tabPath);
                first = false;
            } else {
                openInNewTab(tabPath);
            }

            if (viewMode >= static_cast<int>(ViewMode::Icon) &&
                viewMode <= static_cast<int>(ViewMode::Gallery)) {
                if (TabPage* page = m_tabManager->currentPage()) {
                    page->setViewMode(static_cast<ViewMode>(viewMode));
                }
            }
        }
        if (first) {
            openPath(AppSettings::instance().defaultPath());
        }
    } else {
        openPath(AppSettings::instance().defaultPath());
    }

    // Purge old Trash items in the background if configured.
    const int purgeDays = AppSettings::instance().trashAutoPurgeDays();
    if (purgeDays > 0) {
        QTimer::singleShot(2000, this, [this, purgeDays]() {
            auto* watcher = new QFutureWatcher<int>(this);
            connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher, purgeDays]() {
                const int purged = watcher->result();
                watcher->deleteLater();
                if (purged > 0) {
                    m_sidebar->refresh();
                    DesktopNotifier::notify(
                        QStringLiteral("Trash cleaned"),
                        QStringLiteral("%1 item(s) older than %2 days were removed from Trash.")
                            .arg(purged)
                            .arg(purgeDays));
                }
            });
            watcher->setFuture(QtConcurrent::run([purgeDays]() {
                return XdgTrash::purgeOlderThan(QDateTime::currentDateTime().addDays(-purgeDays));
            }));
        });
    }
}

MainWindow::~MainWindow() {
    AppSettings::instance().setWindowGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());
    AppSettings::instance().save();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSessionState();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSessionState() {
    AppSettings::instance().setWindowGeometry(saveGeometry());
    AppSettings::instance().setWindowState(saveState());

    QStringList tabPaths;
    for (int i = 0; i < m_tabManager->count(); ++i) {
        if (auto* page = m_tabManager->pageAt(i)) {
            tabPaths.append(QStringLiteral("%1\t%2")
                                .arg(page->currentPath())
                                .arg(static_cast<int>(page->viewMode())));
        }
    }
    AppSettings::instance().setSavedTabs(tabPaths);
    AppSettings::instance().save();
}

void MainWindow::setActivationToken(const QString& token) {
#ifdef HAVE_KWINDOWSYSTEM
    if (!token.isEmpty()) {
        KWindowSystem::setCurrentXdgActivationToken(token);
    }
#else
    Q_UNUSED(token);
#endif
}

void MainWindow::activateFromDesktop() {
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();
    activateWindow();
    if (QWindow* handle = windowHandle()) {
        handle->requestActivate();
#ifdef HAVE_KWINDOWSYSTEM
        // On Wayland, focus-stealing prevention needs the xdg-activation
        // protocol; KWindowSystem knows how to request it correctly on Plasma.
        KWindowSystem::activateWindow(handle);
        // Activation tokens are single-use; do not leak one into later calls.
        KWindowSystem::setCurrentXdgActivationToken(QString());
#endif
    }

    // A compositor can still refuse a focus change coming from a background
    // process. On KDE Wayland, fall back to asking KWin to activate our window.
    QTimer::singleShot(150, this, [this]() {
        if (!isActiveWindow()) {
            requestKWinActivation();
        }
    });
}

void MainWindow::requestKWinActivation() {
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    const QString session = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (!desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
        session.compare(QStringLiteral("wayland"), Qt::CaseInsensitive) != 0) {
        return;
    }
    if (m_kwinActivationPending) {
        return;
    }
    m_kwinActivationPending = true;

    const qint64 pid = QCoreApplication::applicationPid();
    const QString scriptName = QStringLiteral("finder-activate-%1").arg(pid);
    const QString scriptPath =
        QDir::tempPath() + QStringLiteral("/finder-linux-activate-%1.js").arg(pid);

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        m_kwinActivationPending = false;
        return;
    }

    QTextStream out(&script);
    out << "const targetPid = " << pid << ";\n"
        << "for (const w of workspace.windowList()) {\n"
        << "    if (w.pid === targetPid) {\n"
        << "        workspace.activeWindow = w;\n"
        << "        break;\n"
        << "    }\n"
        << "}\n";
    script.close();

    auto sendScriptingCall = [](const QString& method, const QVariantList& args) {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"), method);
        message.setArguments(args);
        QDBusConnection::sessionBus().send(message);
    };

    sendScriptingCall(QStringLiteral("unloadScript"), {scriptName});
    sendScriptingCall(QStringLiteral("loadScript"), {scriptPath, scriptName});
    sendScriptingCall(QStringLiteral("start"), {});

    QTimer::singleShot(1000, this, [this, scriptName, scriptPath]() {
        QDBusMessage unload = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
        unload.setArguments({scriptName});
        QDBusConnection::sessionBus().send(unload);
        QFile::remove(scriptPath);
        m_kwinActivationPending = false;
    });
}

void MainWindow::activateDesktopAction(const QString& actionName, const QVariantList& parameter) {
    Q_UNUSED(parameter);
    const QString action = actionName.toLower();

    if (action == QStringLiteral("newwindow") || action == QStringLiteral("new-window")) {
        QString path = AppSettings::instance().defaultPath();
        if (TabPage* page = m_tabManager->currentPage()) {
            path = page->currentPath();
        }
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("--new-window"), path});
        return;
    }

    if (action == QStringLiteral("newtab") || action == QStringLiteral("new-tab")) {
        TabPage* page = m_tabManager->currentPage();
        m_tabManager->createTab(page ? page->currentPath() : AppSettings::instance().defaultPath());
        activateFromDesktop();
        return;
    }

    // Unknown action: at least bring the window to the front.
    activateFromDesktop();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::BackButton) {
            onNavigateBack();
            return true;
        } else if (me->button() == Qt::ForwardButton) {
            onNavigateForward();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("File Manager"));

    // Toolbar
    m_toolbar = new Toolbar(this);
    m_toolbar->setObjectName(QStringLiteral("mainToolBar"));
    addToolBar(Qt::TopToolBarArea, m_toolbar);

    connect(m_toolbar, &Toolbar::backRequested, this, &MainWindow::onNavigateBack);
    connect(m_toolbar, &Toolbar::forwardRequested, this, &MainWindow::onNavigateForward);
    connect(m_toolbar, &Toolbar::pathChanged, this, &MainWindow::onPathChanged);
    connect(m_toolbar, &Toolbar::viewModeChanged, this, &MainWindow::onViewModeChanged);
    connect(m_toolbar, &Toolbar::newFolderRequested, this, &MainWindow::onNewFolder);
    connect(m_toolbar, &Toolbar::quickLookRequested, this, &MainWindow::onQuickLook);
    connect(m_toolbar, &Toolbar::getInfoRequested, this, &MainWindow::onGetInfo);
    connect(m_toolbar, &Toolbar::toggleHiddenRequested, this, &MainWindow::onToggleHidden);
    connect(m_toolbar, &Toolbar::openTerminalRequested, this, &MainWindow::toggleTerminalPanel);
    connect(m_toolbar, &Toolbar::searchTextChanged, this, &MainWindow::onSearchTextChanged);
    connect(m_toolbar, &Toolbar::viewOptionsRequested, this, &MainWindow::onViewOptions);
    connect(m_toolbar, &Toolbar::tagRequested, this, [this](const QString& tag) {
        if (TabPage* page = m_tabManager->currentPage()) {
            const QStringList paths = page->selectedFilePaths();
            for (const QString& path : paths) {
                TagManager::instance().addTag(path, tag);
            }
            if (!paths.isEmpty())
                page->refresh();
        }
    });
    connect(m_toolbar, &Toolbar::clearTagsRequested, this, [this]() {
        if (TabPage* page = m_tabManager->currentPage()) {
            const QStringList paths = page->selectedFilePaths();
            for (const QString& path : paths) {
                for (const auto& tag : TagManager::standardTags()) {
                    TagManager::instance().removeTag(path, tag.name);
                }
            }
            if (!paths.isEmpty())
                page->refresh();
        }
    });

    // Splitter: Sidebar + TabManager
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setHandleWidth(1);

    m_sidebar = new Sidebar(m_mainSplitter);
    m_sidebar->setFixedWidth(210);
    connect(m_sidebar, &Sidebar::pathSelected, this, &MainWindow::onPathChanged);
    connect(m_sidebar, &Sidebar::openInNewTabRequested, this,
            [this](const QString& p) { m_tabManager->createTab(p); });
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
    connect(m_sidebar, &Sidebar::itemsDroppedOnTrash, this,
            [this](const QStringList& sources) { m_opEngine->trashItems(sources); });
    connect(m_sidebar, &Sidebar::tagSelected, this, [this](const QString& tag) {
        auto* page = m_tabManager->currentPage();
        if (page) {
            page->showTaggedFiles(tag);
        }
    });
    connect(m_sidebar, &Sidebar::serverSelected, this, &MainWindow::connectToServerAsync);
    connect(m_sidebar, &Sidebar::newSmartFolderRequested, this, &MainWindow::onNewSmartFolder);
    connect(m_sidebar, &Sidebar::editSmartFolderRequested, this, &MainWindow::onEditSmartFolder);
    connect(m_sidebar, &Sidebar::deleteSmartFolderRequested, this,
            &MainWindow::onDeleteSmartFolder);

    m_tabManager = new TabManager(m_mainSplitter);
    connect(m_tabManager, &TabManager::activePageChanged, this, &MainWindow::onActivePageChanged);
    connect(m_tabManager, &TabManager::activePagePathChanged, this,
            [this](TabPage* page, const QString& p) {
                if (page == m_tabManager->currentPage()) {
                    m_toolbar->setPath(p);
                    m_toolbar->setViewMode(page->viewMode());
                    m_toolbar->setSearchText(page->searchText());
                    m_sidebar->selectPath(p);
                    if (m_terminalPanel && m_terminalPanel->isVisible()) {
                        m_terminalPanel->setCurrentDirectory(p);
                    }
                    updateNavigationState();
                    updateStatusInfo();
                }
            });
    connect(m_tabManager, &TabManager::activePageHistoryChanged, this, [this](TabPage* page) {
        if (page == m_tabManager->currentPage()) {
            updateNavigationState();
        }
    });
    connect(m_tabManager, &TabManager::activePageStatusChanged, this,
            [this](const QString& msg) { m_statusItemCountLabel->setText(msg); });
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
    connect(m_tabManager, &TabManager::contextMenuRequested, this,
            &MainWindow::onContextMenuRequested);
    connect(m_tabManager, &TabManager::fileDropRequested, this, &MainWindow::onFilesDropped);
    connect(m_tabManager, &TabManager::searchClearRequested, this,
            [this]() { m_toolbar->clearSearch(); });
    connect(m_tabManager, &TabManager::saveSearchRequested, this, &MainWindow::onSaveSearch);
    connect(m_tabManager, &TabManager::itemRenamed, this,
            [this](const QString& oldPath, const QString& newPath) {
                UndoAction act;
                act.type = UndoActionType::Rename;
                act.description =
                    QStringLiteral("Rename \"%1\" to \"%2\"")
                        .arg(QFileInfo(oldPath).fileName(), QFileInfo(newPath).fileName());
                act.sourcePath = oldPath;
                act.targetPath = newPath;
                m_undoManager->pushAction(act);
            });

    m_mainSplitter->addWidget(m_sidebar);
    m_mainSplitter->addWidget(m_tabManager);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    // The splitter sizes children from their size hints (QTreeWidget reports
    // 256), which leaves a gap next to the fixed-width sidebar.
    m_mainSplitter->setSizes({m_sidebar->minimumWidth(), 1});

    m_terminalPanel = new TerminalPanel(this);
    m_terminalPanel->hide();
    connect(m_terminalPanel, &TerminalPanel::directoryChanged, this, [this](const QString& dir) {
        if (TabPage* page = m_tabManager->currentPage()) {
            page->navigateTo(dir);
        }
    });
    connect(m_terminalPanel, &TerminalPanel::closeRequested, this,
            &MainWindow::toggleTerminalPanel);

    m_verticalSplitter = new QSplitter(Qt::Vertical, this);
    m_verticalSplitter->setHandleWidth(1);
    m_verticalSplitter->addWidget(m_mainSplitter);
    m_verticalSplitter->addWidget(m_terminalPanel);
    m_verticalSplitter->setStretchFactor(0, 1);
    m_verticalSplitter->setStretchFactor(1, 0);

    setCentralWidget(m_verticalSplitter);

    // Status bar
    auto* sBar = statusBar();
    m_statusItemCountLabel = new QLabel(this);
    m_statusDiskFreeLabel = new QLabel(this);

    sBar->addWidget(m_statusItemCountLabel);
    sBar->addPermanentWidget(m_statusDiskFreeLabel);
}

void MainWindow::setupMenusAndShortcuts() {
    auto* mBar = menuBar();

    // File Menu
    auto* fileMenu = mBar->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(
        QStringLiteral("New Tab"), QKeySequence(Qt::CTRL | Qt::Key_T), this, [this]() {
            TabPage* curr = m_tabManager->currentPage();
            QString path = curr ? curr->currentPath() : AppSettings::instance().defaultPath();
            m_tabManager->createTab(path);
        });
    fileMenu->addAction(QStringLiteral("New Window"), QKeySequence::New, this, [this]() {
        QString path = AppSettings::instance().defaultPath();
        if (TabPage* page = m_tabManager->currentPage()) {
            path = page->currentPath();
        }
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                {QStringLiteral("--new-window"), path});
    });

    fileMenu->addAction(QStringLiteral("New Folder"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this,
                        &MainWindow::onNewFolder);
    fileMenu->addAction(QStringLiteral("New Text File..."),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), this,
                        &MainWindow::onNewTextFile);
    auto* recentMenu = fileMenu->addMenu(QStringLiteral("Open Recent"));
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
        recentMenu->clear();
        const QList<RecentFile> recent = RecentFilesManager::list(15);
        if (recent.isEmpty()) {
            QAction* empty = recentMenu->addAction(QStringLiteral("No Recent Files"));
            empty->setEnabled(false);
            return;
        }

        for (const RecentFile& entry : recent) {
            const QFileInfo info(entry.path);
            QString label = info.fileName();
            if (label.isEmpty()) {
                label = entry.path;
            }
            QAction* action = recentMenu->addAction(
                QIcon::fromTheme(QStringLiteral("document-open")), label, this,
                [this, path = entry.path]() {
                    const QFileInfo recentInfo(path);
                    if (recentInfo.isDir()) {
                        openInNewTab(path);
                    } else if (!DesktopAppsManager::launchFile(path)) {
                        QMessageBox::warning(this, QStringLiteral("Cannot Open Recent File"),
                                             QStringLiteral("Could not open \"%1\".").arg(path));
                    }
                });
            action->setToolTip(entry.path);
        }

        recentMenu->addSeparator();
        recentMenu->addAction(QStringLiteral("Clear Recent Files"), this, [this, recentMenu]() {
            if (QMessageBox::question(
                    this, QStringLiteral("Clear Recent Files"),
                    QStringLiteral("Clear the recently used files list for all applications?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
                RecentFilesManager::clear();
                recentMenu->clear();
            }
        });
    });
    fileMenu->addAction(QStringLiteral("Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this,
                        [this]() { m_tabManager->closeCurrentTab(); });
    fileMenu->addAction(QStringLiteral("Reopen Closed Tab"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), this,
                        [this]() { m_tabManager->reopenLastClosedTab(); });

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Get Info"), QKeySequence(Qt::CTRL | Qt::Key_I), this,
                        &MainWindow::onGetInfo);
    fileMenu->addAction(QStringLiteral("Quick Look"), QKeySequence(Qt::Key_Space), this,
                        &MainWindow::onQuickLook);
    fileMenu->addAction(QStringLiteral("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), this,
                        &MainWindow::onDuplicate);
    fileMenu->addAction(QStringLiteral("Compress"), this,
                        [this]() { onCompress(QStringLiteral("zip")); });
    fileMenu->addAction(QStringLiteral("Extract Here"), this, &MainWindow::onExtractHere);
    fileMenu->addAction(QStringLiteral("Make Link / Alias"), this, &MainWindow::onMakeSymlink);
    fileMenu->addAction(QStringLiteral("Copy Path"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                        this, &MainWindow::onCopyPath);

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Move to Trash"), QKeySequence(Qt::Key_Delete), this,
                        &MainWindow::onMoveToTrash);
    fileMenu->addAction(QStringLiteral("Delete Permanently"),
                        QKeySequence(Qt::SHIFT | Qt::Key_Delete), this,
                        &MainWindow::onDeletePermanently);
    fileMenu->addAction(QStringLiteral("Put Back"), this, &MainWindow::onPutBack);
    fileMenu->addAction(QStringLiteral("Empty Trash..."), this, &MainWindow::onEmptyTrash);

    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), QKeySequence(Qt::CTRL | Qt::Key_Q), this,
                        &QWidget::close);

    // Edit Menu
    auto* editMenu = mBar->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("Undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), this,
                        &MainWindow::onUndo);
    editMenu->addAction(QStringLiteral("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
                        this, &MainWindow::onRedo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Cut"), QKeySequence(Qt::CTRL | Qt::Key_X), this,
                        &MainWindow::onCut);
    editMenu->addAction(QStringLiteral("Copy"), QKeySequence(Qt::CTRL | Qt::Key_C), this,
                        &MainWindow::onCopy);
    editMenu->addAction(QStringLiteral("Paste"), QKeySequence(Qt::CTRL | Qt::Key_V), this,
                        &MainWindow::onPaste);
    m_copyToOtherPaneAction = editMenu->addAction(QStringLiteral("Copy to Other Pane"), this,
                                                  &MainWindow::onCopyToOtherPane);
    m_moveToOtherPaneAction = editMenu->addAction(QStringLiteral("Move to Other Pane"), this,
                                                  &MainWindow::onMoveToOtherPane);
    m_copyToOtherPaneAction->setEnabled(false);
    m_moveToOtherPaneAction->setEnabled(false);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Rename"), QKeySequence(Qt::Key_F2), this,
                        &MainWindow::onRename);
    editMenu->addAction(QStringLiteral("Batch Rename..."),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this,
                        &MainWindow::onBatchRename);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Find..."), QKeySequence::Find, this,
                        [this]() { m_toolbar->focusSearch(); });
    editMenu->addAction(QStringLiteral("Select by Pattern..."), this,
                        &MainWindow::onSelectByPattern);
    editMenu->addAction(QStringLiteral("Invert Selection"), this, &MainWindow::onInvertSelection);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Preferences..."), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                        this, &MainWindow::onPreferences);

    // View Menu
    auto* viewMenu = mBar->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("as Icons"), QKeySequence(Qt::CTRL | Qt::Key_1), this,
                        [this]() { onViewModeChanged(ViewMode::Icon); });
    viewMenu->addAction(QStringLiteral("as List"), QKeySequence(Qt::CTRL | Qt::Key_2), this,
                        [this]() { onViewModeChanged(ViewMode::List); });
    viewMenu->addAction(QStringLiteral("as Columns"), QKeySequence(Qt::CTRL | Qt::Key_3), this,
                        [this]() { onViewModeChanged(ViewMode::Column); });
    viewMenu->addAction(QStringLiteral("as Gallery"), QKeySequence(Qt::CTRL | Qt::Key_4), this,
                        [this]() { onViewModeChanged(ViewMode::Gallery); });
    viewMenu->addAction(QStringLiteral("Show View Options..."), QKeySequence(Qt::CTRL | Qt::Key_J),
                        this, &MainWindow::onViewOptions);
    viewMenu->addSeparator();
    auto* splitAction =
        viewMenu->addAction(QStringLiteral("Split View"), this, &MainWindow::onToggleSplitView);
    splitAction->setShortcut(QKeySequence(Qt::Key_F3));
    m_syncSplitAction = viewMenu->addAction(QStringLiteral("Synchronize Split Panes"));
    m_syncSplitAction->setCheckable(true);
    m_syncSplitAction->setEnabled(false);
    connect(m_syncSplitAction, &QAction::toggled, this,
            [this](bool checked) { m_tabManager->setSplitSyncEnabled(checked); });
    auto* terminalAction = viewMenu->addAction(QStringLiteral("Terminal Panel"), this,
                                               &MainWindow::toggleTerminalPanel);
    terminalAction->setShortcut(QKeySequence(Qt::Key_F4));
    viewMenu->addSeparator();
    m_toggleHiddenAction =
        viewMenu->addAction(QStringLiteral("Toggle Hidden Files"),
                            QKeySequence(Qt::CTRL | Qt::Key_H), this, &MainWindow::onToggleHidden);
    m_toggleHiddenAction->setCheckable(true);
    m_toggleHiddenAction->setChecked(AppSettings::instance().showHiddenFiles());

    auto* calcFolderSizesAction =
        viewMenu->addAction(QStringLiteral("Calculate Folder Sizes"), this, [this](bool checked) {
            AppSettings::instance().setCalculateFolderSizes(checked);
            m_tabManager->setCalculateFolderSizes(checked);
        });
    calcFolderSizesAction->setCheckable(true);
    calcFolderSizesAction->setChecked(AppSettings::instance().calculateFolderSizes());
    viewMenu->addAction(QStringLiteral("Customize Sidebar..."), m_sidebar,
                        &Sidebar::openManageSidebarDialog);
    viewMenu->addAction(QStringLiteral("Clean Up"), this, [this]() {
        if (TabPage* page = m_tabManager->currentPage())
            page->cleanUpIconView();
    });
    viewMenu->addAction(QStringLiteral("Align to Grid"), this, [this]() {
        if (TabPage* page = m_tabManager->currentPage())
            page->cleanUpIconView();
    });

    viewMenu->addSeparator();
    m_showSidebarAction = viewMenu->addAction(QStringLiteral("Show Sidebar"));
    m_showSidebarAction->setCheckable(true);
    m_showSidebarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    m_showSidebarAction->setChecked(AppSettings::instance().showSidebar());
    connect(m_showSidebarAction, &QAction::toggled, this, [this](bool checked) {
        AppSettings::instance().setShowSidebar(checked);
        AppSettings::instance().save();
        m_sidebar->setVisible(checked);
    });

    m_showToolbarAction = viewMenu->addAction(QStringLiteral("Show Toolbar"));
    m_showToolbarAction->setCheckable(true);
    m_showToolbarAction->setChecked(AppSettings::instance().showToolbar());
    connect(m_showToolbarAction, &QAction::toggled, this, [this](bool checked) {
        AppSettings::instance().setShowToolbar(checked);
        AppSettings::instance().save();
        m_toolbar->setVisible(checked);
    });

    m_showPathBarAction = viewMenu->addAction(QStringLiteral("Show Path Bar"));
    m_showPathBarAction->setCheckable(true);
    m_showPathBarAction->setChecked(AppSettings::instance().showPathBar());
    connect(m_showPathBarAction, &QAction::toggled, this, [this](bool checked) {
        AppSettings::instance().setShowPathBar(checked);
        AppSettings::instance().save();
        m_toolbar->setPathBarVisible(checked);
    });

    m_showPreviewPaneAction = viewMenu->addAction(QStringLiteral("Show Preview Pane"));
    m_showPreviewPaneAction->setCheckable(true);
    m_showPreviewPaneAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    m_showPreviewPaneAction->setChecked(AppSettings::instance().showPreviewPane());
    connect(m_showPreviewPaneAction, &QAction::toggled, this, [this](bool checked) {
        AppSettings::instance().setShowPreviewPane(checked);
        AppSettings::instance().save();
        for (int i = 0; i < m_tabManager->count(); ++i) {
            if (auto* split = qobject_cast<SplitTabWidget*>(m_tabManager->widget(i))) {
                if (split->primaryPage())
                    split->primaryPage()->setPreviewPaneVisible(checked);
                if (split->secondaryPage())
                    split->secondaryPage()->setPreviewPaneVisible(checked);
            }
        }
    });

    m_showStatusBarAction = viewMenu->addAction(QStringLiteral("Show Status Bar"));
    m_showStatusBarAction->setCheckable(true);
    m_showStatusBarAction->setChecked(AppSettings::instance().showStatusBar());
    connect(m_showStatusBarAction, &QAction::toggled, this, [this](bool checked) {
        AppSettings::instance().setShowStatusBar(checked);
        AppSettings::instance().save();
        statusBar()->setVisible(checked);
    });

    m_fullScreenAction = viewMenu->addAction(QStringLiteral("Toggle Full Screen"));
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    connect(m_fullScreenAction, &QAction::triggered, this, [this]() {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });

    auto* refreshAction = viewMenu->addAction(QStringLiteral("Refresh"), this, [this]() {
        m_tabManager->refreshCurrentTab();
        m_sidebar->refresh();
    });
    refreshAction->setShortcuts({QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::Key_R)});

    viewMenu->addSeparator();
    auto* sortMenu = viewMenu->addMenu(QStringLiteral("Sort By"));
    auto* sortGroup = new QActionGroup(this);
    sortGroup->setExclusive(true);
    auto addSortAction = [&](const QString& label, int column) {
        QAction* action = sortMenu->addAction(label);
        action->setCheckable(true);
        action->setData(column);
        sortGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, column]() {
            TabPage* page = m_tabManager->currentPage();
            const Qt::SortOrder order =
                page ? page->sortOrder() : AppSettings::instance().sortOrder();
            applySort(column, order);
        });
    };
    addSortAction(QStringLiteral("Name"), FileSystemModel::ColName);
    addSortAction(QStringLiteral("Date Modified"), FileSystemModel::ColDateModified);
    addSortAction(QStringLiteral("Size"), FileSystemModel::ColSize);
    addSortAction(QStringLiteral("Kind"), FileSystemModel::ColKind);

    sortMenu->addSeparator();
    auto* orderGroup = new QActionGroup(this);
    orderGroup->setExclusive(true);
    auto* ascendingAction = sortMenu->addAction(QStringLiteral("Ascending"));
    ascendingAction->setCheckable(true);
    orderGroup->addAction(ascendingAction);
    auto* descendingAction = sortMenu->addAction(QStringLiteral("Descending"));
    descendingAction->setCheckable(true);
    orderGroup->addAction(descendingAction);
    connect(ascendingAction, &QAction::triggered, this, [this]() {
        TabPage* page = m_tabManager->currentPage();
        const int column = page ? page->sortColumn() : AppSettings::instance().sortColumn();
        applySort(column, Qt::AscendingOrder);
    });
    connect(descendingAction, &QAction::triggered, this, [this]() {
        TabPage* page = m_tabManager->currentPage();
        const int column = page ? page->sortColumn() : AppSettings::instance().sortColumn();
        applySort(column, Qt::DescendingOrder);
    });

    QAction* dirsFirstAction = viewMenu->addAction(QStringLiteral("Keep Folders on Top"));
    dirsFirstAction->setCheckable(true);
    connect(dirsFirstAction, &QAction::toggled, this, [this](bool checked) {
        AppSettings::instance().setDirectoriesFirst(checked);
        AppSettings::instance().save();
        for (int i = 0; i < m_tabManager->count(); ++i) {
            if (auto* split = qobject_cast<SplitTabWidget*>(m_tabManager->widget(i))) {
                if (split->primaryPage())
                    split->primaryPage()->setDirectoriesFirst(checked);
                if (split->secondaryPage())
                    split->secondaryPage()->setDirectoriesFirst(checked);
            }
        }
    });

    // Header clicks also change the sort; keep the menu in sync when opened.
    connect(sortMenu, &QMenu::aboutToShow, this,
            [sortGroup, ascendingAction, descendingAction, dirsFirstAction]() {
                const int column = AppSettings::instance().sortColumn();
                for (QAction* action : sortGroup->actions()) {
                    action->setChecked(action->data().toInt() == column);
                }
                ascendingAction->setChecked(AppSettings::instance().sortOrder() ==
                                            Qt::AscendingOrder);
                descendingAction->setChecked(AppSettings::instance().sortOrder() ==
                                             Qt::DescendingOrder);
                dirsFirstAction->setChecked(AppSettings::instance().directoriesFirst());
            });

    viewMenu->addSeparator();
    QAction* zoomInAction =
        viewMenu->addAction(QStringLiteral("Zoom In"), this, [this]() { adjustIconSize(16); });
    zoomInAction->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    QAction* zoomOutAction =
        viewMenu->addAction(QStringLiteral("Zoom Out"), this, [this]() { adjustIconSize(-16); });
    zoomOutAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Minus)});
    viewMenu->addAction(QStringLiteral("Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0), this,
                        [this]() { applyIconSizeToAll(64); });

    auto* nextTabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(nextTabShortcut, &QShortcut::activated, this,
            [this]() { m_tabManager->activateNextTab(); });
    auto* prevTabShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    connect(prevTabShortcut, &QShortcut::activated, this,
            [this]() { m_tabManager->activatePreviousTab(); });

    // Go Menu
    auto* goMenu = mBar->addMenu(QStringLiteral("&Go"));
    m_backAction = goMenu->addAction(QStringLiteral("Back"), this, &MainWindow::onNavigateBack);
    m_backAction->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Left), QKeySequence::Back});
    m_backAction->setEnabled(false);

    m_fwdAction =
        goMenu->addAction(QStringLiteral("Forward"), this, &MainWindow::onNavigateForward);
    m_fwdAction->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Right), QKeySequence::Forward});
    m_fwdAction->setEnabled(false);

    goMenu->addAction(QStringLiteral("Enclosing Folder"), QKeySequence(Qt::ALT | Qt::Key_Up), this,
                      &MainWindow::onNavigateUp);
    goMenu->addSeparator();
    goMenu->addAction(QStringLiteral("Go to Folder..."), QKeySequence(Qt::CTRL | Qt::Key_L), this,
                      [this]() { m_toolbar->breadcrumbBar()->showPathEditor(); });
    goMenu->addAction(QStringLiteral("Home"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this,
                      [this]() { openPath(QDir::homePath()); });
    goMenu->addSeparator();
    goMenu->addAction(QStringLiteral("Connect to Server..."), QKeySequence(Qt::CTRL | Qt::Key_K),
                      this, [this]() {
                          bool ok = false;
                          const QString uri = QInputDialog::getText(
                              this, QStringLiteral("Connect to Server"),
                              QStringLiteral("Server address (sftp://user@host, "
                                             "smb://server/share, ftp://host, dav://host):"),
                              QLineEdit::Normal, QStringLiteral("sftp://"), &ok);
                          if (ok && !uri.trimmed().isEmpty()) {
                              connectToServerAsync(uri.trimmed());
                          }
                      });
    goMenu->addAction(QStringLiteral("Trash..."), this, [this]() {
        TrashDialog dialog(this);
        connect(&dialog, &TrashDialog::trashChanged, this, [this]() {
            m_sidebar->refresh();
            if (TabPage* page = m_tabManager->currentPage()) {
                if (page->currentPath().startsWith(XdgTrash::trashDirectory())) {
                    page->refresh();
                }
            }
        });
        dialog.exec();
    });

    // Theme Menu
    auto* themeMenu = mBar->addMenu(QStringLiteral("&Theme"));
    themeMenu->addAction(QStringLiteral("Light"), this, [this]() {
        AppSettings::instance().setTheme(AppTheme::Light);
        ThemeManager::instance().applyTheme(AppTheme::Light);
        if (auto* p = m_tabManager->currentPage())
            p->refresh();
        m_sidebar->refresh();
    });
    themeMenu->addAction(QStringLiteral("Dark"), this, [this]() {
        AppSettings::instance().setTheme(AppTheme::Dark);
        ThemeManager::instance().applyTheme(AppTheme::Dark);
        if (auto* p = m_tabManager->currentPage())
            p->refresh();
        m_sidebar->refresh();
    });
    themeMenu->addAction(QStringLiteral("Follow System"), this, [this]() {
        AppSettings::instance().setTheme(AppTheme::System);
        ThemeManager::instance().applyTheme(AppTheme::System);
        if (auto* p = m_tabManager->currentPage())
            p->refresh();
        m_sidebar->refresh();
    });
}

void MainWindow::openPath(const QString& path) {
    TabPage* page = m_tabManager->currentPage();
    if (!page) {
        m_tabManager->createTab(path);
    } else {
        page->navigateTo(path);
    }
}

void MainWindow::openInNewTab(const QString& path) {
    m_tabManager->createTab(path);
}

void MainWindow::openAndSelect(const QString& filePath) {
    QFileInfo fi(filePath);
    if (!fi.exists() && !fi.isSymLink()) {
        openPath(filePath);
        return;
    }
    QString folder = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    openPath(folder);
    if (TabPage* page = m_tabManager->currentPage()) {
        page->refresh(fi.absoluteFilePath());
    }
}

void MainWindow::openRemoteLocation(const QString& uri) {
    connectToServerAsync(uri);
}

void MainWindow::showItems(const QStringList& paths) {
    QStringList directories;
    QStringList files;
    QStringList remoteUris;

    for (const QString& item : paths) {
        if (item.isEmpty())
            continue;

        const QUrl url(item);
        if (!url.isLocalFile() && url.isValid() && !url.scheme().isEmpty()) {
            remoteUris.append(item);
            continue;
        }

        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : item);
        if (info.isDir()) {
            directories.append(info.absoluteFilePath());
        } else if (info.exists() || info.isSymLink()) {
            files.append(info.absoluteFilePath());
        }
    }

    for (const QString& uri : remoteUris) {
        openRemoteLocation(uri);
    }

    bool openedAny = false;
    auto presentDirectory = [&](const QString& directory, const QStringList& selection) {
        if (!openedAny) {
            openPath(directory);
        } else {
            openInNewTab(directory);
        }
        openedAny = true;

        if (!selection.isEmpty()) {
            if (TabPage* page = m_tabManager->currentPage()) {
                page->refreshWithSelection(selection);
            }
        }
    };

    for (const QString& directory : directories) {
        presentDirectory(directory, {});
    }

    // Group files by parent folder so that a multi-file request opens one
    // tab per folder instead of one tab per file.
    QStringList directoryOrder;
    QHash<QString, QStringList> filesByDirectory;
    for (const QString& file : files) {
        const QString directory = QFileInfo(file).absolutePath();
        if (!filesByDirectory.contains(directory)) {
            directoryOrder.append(directory);
        }
        filesByDirectory[directory].append(file);
    }
    for (const QString& directory : directoryOrder) {
        presentDirectory(directory, filesByDirectory.value(directory));
    }

    // Nothing on disk matched (for example a stale download entry): fall back
    // to the normal open path so the user still gets a window.
    if (!openedAny && !remoteUris.isEmpty()) {
        openedAny = true;
    }
    if (!openedAny && !paths.isEmpty()) {
        openPath(paths.first());
    }

    activateFromDesktop();
}

void MainWindow::showItemProperties(const QStringList& paths) {
    QStringList validPaths;
    for (const QString& item : paths) {
        if (item.isEmpty())
            continue;
        const QUrl url(item);
        const QString path = url.isLocalFile() ? url.toLocalFile() : item;
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink() && !info.isDir())
            continue;
        validPaths.append(info.absoluteFilePath());
    }

    if (validPaths.isEmpty()) {
        onGetInfo();
        return;
    }

    auto* dlg = new GetInfoDialog(validPaths, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    activateFromDesktop();
}

void MainWindow::onNavigateBack() {
    if (TabPage* page = m_tabManager->currentPage()) {
        page->goBack();
    }
}

void MainWindow::onNavigateForward() {
    if (TabPage* page = m_tabManager->currentPage()) {
        page->goForward();
    }
}

void MainWindow::onNavigateUp() {
    if (TabPage* page = m_tabManager->currentPage()) {
        page->goUp();
    }
}

void MainWindow::onPathChanged(const QString& path) {
    openPath(path);
}

void MainWindow::onViewModeChanged(ViewMode mode) {
    AppSettings::instance().setViewMode(mode);
    m_toolbar->setViewMode(mode);
    if (TabPage* page = m_tabManager->currentPage()) {
        page->setViewMode(mode);
    }
}

void MainWindow::onToggleSplitView() {
    m_tabManager->toggleSplit();
    updateSplitActions();
}

void MainWindow::onActivePageChanged(TabPage* page) {
    if (!page)
        return;

    m_toolbar->setPath(page->currentPath());
    m_toolbar->setViewMode(page->viewMode());
    m_toolbar->setSearchText(page->searchText());
    m_sidebar->selectPath(page->currentPath());

    if (m_terminalPanel && m_terminalPanel->isVisible()) {
        m_terminalPanel->setCurrentDirectory(page->currentPath());
    }

    updateNavigationState();
    updateStatusInfo();
    updateSplitActions();
}

void MainWindow::toggleTerminalPanel() {
    if (!m_terminalPanel)
        return;
    bool show = !m_terminalPanel->isVisible();
    m_terminalPanel->setVisible(show);
    if (show) {
        if (TabPage* page = m_tabManager->currentPage()) {
            m_terminalPanel->setCurrentDirectory(page->currentPath());
        }
        m_terminalPanel->focusInput();
        m_verticalSplitter->setSizes({qMax(300, height() - 200), 200});
    }
}

void MainWindow::updateNavigationState() {
    if (TabPage* page = m_tabManager->currentPage()) {
        bool canBack = page->canGoBack();
        bool canFwd = page->canGoForward();
        m_toolbar->setCanGoBack(canBack);
        m_toolbar->setCanGoForward(canFwd);
        if (m_backAction)
            m_backAction->setEnabled(canBack);
        if (m_fwdAction)
            m_fwdAction->setEnabled(canFwd);
    } else {
        m_toolbar->setCanGoBack(false);
        m_toolbar->setCanGoForward(false);
        if (m_backAction)
            m_backAction->setEnabled(false);
        if (m_fwdAction)
            m_fwdAction->setEnabled(false);
    }
}

void MainWindow::updateStatusInfo() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStorageInfo storage(page->currentPath());
    if (storage.isValid()) {
        m_statusDiskFreeLabel->setText(
            QStringLiteral("%1 available").arg(FileItem::formatSize(storage.bytesAvailable())));
    }
}

bool MainWindow::ensureOperationsIdle() {
    if (!m_opEngine->isRunning()) {
        return true;
    }
    QMessageBox::information(this, QStringLiteral("Operation in Progress"),
                             QStringLiteral("Another file operation is still running. Please wait "
                                            "for it to finish, then try again."));
    return false;
}

void MainWindow::onNewFolder() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    bool ok = false;
    QString name =
        QInputDialog::getText(this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"),
                              QLineEdit::Normal, QStringLiteral("untitled folder"), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        QString createdPath, error;
        if (m_opEngine->createFolder(page->currentPath(), name.trimmed(), &createdPath, &error)) {
            UndoAction act;
            act.type = UndoActionType::Create;
            act.description = QStringLiteral("Create \"%1\"").arg(name.trimmed());
            act.targetPath = createdPath;
            act.isDirectory = true;
            act.createdSize = 0;
            act.createdModified = QFileInfo(createdPath).lastModified();
            m_undoManager->pushAction(act);
            page->refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"), error);
        }
    }
}

void MainWindow::onNewTextFile() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    bool ok = false;
    QString name =
        QInputDialog::getText(this, QStringLiteral("New File"), QStringLiteral("File name:"),
                              QLineEdit::Normal, QStringLiteral("untitled.txt"), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        QString createdPath, error;
        if (m_opEngine->createFile(page->currentPath(), name.trimmed(), &createdPath, &error)) {
            UndoAction act;
            act.type = UndoActionType::Create;
            act.description = QStringLiteral("Create \"%1\"").arg(name.trimmed());
            act.targetPath = createdPath;
            act.isDirectory = false;
            QFileInfo createdFi(createdPath);
            act.createdSize = createdFi.size();
            act.createdModified = createdFi.lastModified();
            m_undoManager->pushAction(act);
            page->refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"), error);
        }
    }
}

void MainWindow::onMakeSymlink() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty())
            paths.append(single);
    }

    for (const auto& p : paths) {
        QFileInfo fi(p);
        QString linkPath = fi.dir().filePath(fi.fileName() + QStringLiteral(" link"));
        m_opEngine->createSymlink(p, linkPath);
    }
    page->refresh();
}

void MainWindow::onCopyPath() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty())
            paths.append(single);
    }
    if (!paths.isEmpty()) {
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
    }
}

void MainWindow::onCopyTo() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    const QStringList sources = existingPathsOnly(page->selectedFilePaths());
    if (sources.isEmpty())
        return;

    const QString destination = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Copy To Folder"), page->currentPath());
    if (destination.isEmpty())
        return;

    m_opEngine->copyItems(sources, destination);
}

void MainWindow::onMoveTo() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    const QStringList sources = existingPathsOnly(page->selectedFilePaths());
    if (sources.isEmpty())
        return;

    const QString destination = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Move To Folder"), page->currentPath());
    if (destination.isEmpty())
        return;

    m_opEngine->moveItems(sources, destination);
}

void MainWindow::onCopyToOtherPane() {
    TabPage* page = m_tabManager->currentPage();
    TabPage* other = m_tabManager->otherPage();
    if (!page || !other) {
        return;
    }

    const QStringList sources = existingPathsOnly(page->selectedFilePaths());
    if (sources.isEmpty()) {
        return;
    }

    m_opEngine->copyItems(sources, other->currentPath());
}

void MainWindow::onMoveToOtherPane() {
    TabPage* page = m_tabManager->currentPage();
    TabPage* other = m_tabManager->otherPage();
    if (!page || !other) {
        return;
    }

    const QStringList sources = existingPathsOnly(page->selectedFilePaths());
    if (sources.isEmpty()) {
        return;
    }

    m_opEngine->moveItems(sources, other->currentPath());
}

void MainWindow::onNewFolderWithSelection() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    const QStringList sources = existingPathsOnly(page->selectedFilePaths());
    if (sources.isEmpty())
        return;

    const QString parentDirectory = QFileInfo(sources.first()).absolutePath();
    for (const QString& source : sources) {
        if (QFileInfo(source).absolutePath() != parentDirectory) {
            QMessageBox::information(
                this, QStringLiteral("New Folder with Selection"),
                QStringLiteral("All selected items must be in the same folder."));
            return;
        }
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Folder with Selection"),
                                               QStringLiteral("Folder name:"), QLineEdit::Normal,
                                               QStringLiteral("New Folder"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    QString createdPath;
    QString error;
    if (!m_opEngine->createFolder(parentDirectory, name.trimmed(), &createdPath, &error)) {
        QMessageBox::warning(this, QStringLiteral("New Folder with Selection"), error);
        return;
    }

    UndoAction act;
    act.type = UndoActionType::Create;
    act.description = QStringLiteral("Create \"%1\"").arg(name.trimmed());
    act.targetPath = createdPath;
    act.isDirectory = true;
    act.createdSize = 0;
    act.createdModified = QFileInfo(createdPath).lastModified();
    m_undoManager->pushAction(act);

    m_opEngine->moveItems(sources, createdPath);
    page->refresh();
}

void MainWindow::onSelectByPattern() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    bool ok = false;
    const QString pattern =
        QInputDialog::getText(this, QStringLiteral("Select by Pattern"),
                              QStringLiteral("Pattern (for example: *.txt, IMG_*):"),
                              QLineEdit::Normal, QStringLiteral("*"), &ok);
    if (ok && !pattern.trimmed().isEmpty()) {
        page->selectByPattern(pattern);
    }
}

void MainWindow::onInvertSelection() {
    if (TabPage* page = m_tabManager->currentPage()) {
        page->invertSelection();
    }
}

void MainWindow::onCalculateChecksum() {
    TabPage* page = m_tabManager->currentPage();
    if (!page) {
        return;
    }

    QStringList files;
    for (const QString& path : page->selectedFilePaths()) {
        if (!QFileInfo(path).isDir()) {
            files.append(path);
        }
    }
    if (files.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Calculate Checksum"),
            QStringLiteral("Select one or more files to calculate a checksum."));
        return;
    }

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto* progress = new QProgressDialog(QStringLiteral("Calculating SHA-256 checksums..."),
                                         QStringLiteral("Cancel"), 0, 0, this);
    progress->setWindowTitle(QStringLiteral("Calculate Checksums"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    connect(progress, &QProgressDialog::canceled, this, [cancelled]() { cancelled->store(true); });
    progress->show();

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, progress]() {
        progress->close();
        progress->deleteLater();

        const QStringList results = watcher->result();
        watcher->deleteLater();
        if (results.isEmpty()) {
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Checksums"));
        dialog.resize(680, 420);
        auto* layout = new QVBoxLayout(&dialog);
        auto* text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        text->setPlainText(results.join(QStringLiteral("\n\n")));
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        layout->addWidget(text);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto* copyButton = buttons->addButton(QStringLiteral("Copy"), QDialogButtonBox::ActionRole);
        connect(copyButton, &QPushButton::clicked, &dialog,
                [text]() { QApplication::clipboard()->setText(text->toPlainText()); });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    });

    watcher->setFuture(QtConcurrent::run([files, cancelled]() {
        QStringList results;
        constexpr qint64 chunkSize = 1024 * 1024;
        for (const QString& path : files) {
            if (cancelled->load()) {
                results.append(QStringLiteral("%1\nSHA-256: cancelled").arg(path));
                continue;
            }

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                results.append(
                    QStringLiteral("%1\nSHA-256: cannot open: %2").arg(path, file.errorString()));
                continue;
            }

            QCryptographicHash hash(QCryptographicHash::Sha256);
            bool readError = false;
            while (!file.atEnd()) {
                if (cancelled->load()) {
                    break;
                }
                const QByteArray data = file.read(chunkSize);
                if (data.isEmpty() && file.error() != QFileDevice::NoError) {
                    readError = true;
                    break;
                }
                hash.addData(data);
            }

            if (cancelled->load()) {
                results.append(QStringLiteral("%1\nSHA-256: cancelled").arg(path));
            } else if (readError) {
                results.append(
                    QStringLiteral("%1\nSHA-256: read error: %2").arg(path, file.errorString()));
            } else {
                results.append(QStringLiteral("%1\nSHA-256: %2")
                                   .arg(path, QString::fromLatin1(hash.result().toHex())));
            }
        }
        return results;
    }));
}

void MainWindow::onRename() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    if (page->triggerRename()) {
        return;
    }

    QString selected = page->selectedFilePath();
    if (selected.isEmpty())
        return;

    QFileInfo fi(selected);

    QInputDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Rename"));
    dialog.setLabelText(QStringLiteral("New name:"));
    dialog.setTextValue(fi.fileName());
    dialog.resize(
        qBound(520, QFontMetrics(dialog.font()).horizontalAdvance(fi.fileName()) + 180, 900),
        dialog.sizeHint().height());

    // Preselect the base name, keeping the extension untouched.
    if (auto* edit = dialog.findChild<QLineEdit*>()) {
        const QString name = fi.fileName();
        const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
        const int selectionLength = static_cast<int>(dot > 0 ? dot : name.size());
        QTimer::singleShot(0, edit,
                           [edit, selectionLength]() { edit->setSelection(0, selectionLength); });
    }

    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString newName = dialog.textValue();
    if (!newName.trimmed().isEmpty() && newName.trimmed() != fi.fileName()) {
        QString renamedPath, error;
        if (m_opEngine->renameItem(selected, newName.trimmed(), &renamedPath, &error)) {
            UndoAction act;
            act.type = UndoActionType::Rename;
            act.description =
                QStringLiteral("Rename \"%1\" to \"%2\"").arg(fi.fileName(), newName.trimmed());
            act.sourcePath = selected;
            act.targetPath = renamedPath;
            m_undoManager->pushAction(act);
            page->refresh();
        } else {
            QMessageBox::warning(this, QStringLiteral("Error"), error);
        }
    }
}

void MainWindow::onCopy() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    QStringList paths = existingPathsOnly(page->selectedFilePaths());
    if (paths.isEmpty())
        return;
    m_clipboardPaths = paths;
    m_isCutOperation = false;
    setFileClipboard(paths, false);
}

void MainWindow::onCut() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    QStringList paths = existingPathsOnly(page->selectedFilePaths());
    if (paths.isEmpty())
        return;
    m_clipboardPaths = paths;
    m_isCutOperation = true;
    setFileClipboard(paths, true);
}

void MainWindow::onPaste() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    pasteIntoDirectory(page->currentPath());
}

void MainWindow::connectToServerAsync(const QString& uri) {
    if (uri.trimmed().isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto* watcher = new QFutureWatcher<QPair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this, watcher, uri]() {
        QApplication::restoreOverrideCursor();
        const QPair<bool, QString> result = watcher->result();
        watcher->deleteLater();

        if (!result.first) {
            m_sidebar->refresh();
            QMessageBox::warning(this, QStringLiteral("Cannot Connect"),
                                 result.second.isEmpty()
                                     ? QStringLiteral("Could not connect to %1").arg(uri)
                                     : result.second);
            return;
        }

        AppSettings::instance().addRecentServer(uri);
        m_sidebar->refresh();

        // gvfs exposes remote mounts as directories under /run/user/<uid>/gvfs.
        const QString host = QUrl(uri).host();
        const QDir gvfs(QStringLiteral("/run/user/%1/gvfs").arg(::getuid()));
        QString mountPath;
        QDateTime newest;
        for (const auto& entry :
             gvfs.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (!host.isEmpty() && !entry.fileName().contains(host, Qt::CaseInsensitive)) {
                continue;
            }
            if (!newest.isValid() || entry.lastModified() > newest) {
                newest = entry.lastModified();
                mountPath = entry.absoluteFilePath();
            }
        }

        if (mountPath.isEmpty()) {
            QMessageBox::information(
                this, QStringLiteral("Connected"),
                QStringLiteral("%1 was mounted. It should now appear in the sidebar under Devices.")
                    .arg(uri));
            return;
        }

        if (TabPage* page = m_tabManager->currentPage()) {
            page->navigateTo(mountPath);
        } else {
            m_tabManager->createTab(mountPath);
        }
    });

    watcher->setFuture(QtConcurrent::run([uri]() {
        QProcess proc;
        proc.start(QStringLiteral("gio"), {QStringLiteral("mount"), uri});
        if (!proc.waitForFinished(60000) || proc.exitCode() != 0) {
            QString error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            if (error.isEmpty())
                error = QStringLiteral("Could not mount %1").arg(uri);
            return qMakePair(false, error);
        }
        return qMakePair(true, QString());
    }));
}

void MainWindow::applySort(int column, Qt::SortOrder order) {
    AppSettings::instance().setSortColumn(column);
    AppSettings::instance().setSortOrder(order);
    AppSettings::instance().save();

    if (m_tabManager->isSplit()) {
        if (TabPage* page = m_tabManager->currentPage()) {
            page->applySort(column, order);
        }
        return;
    }

    for (int i = 0; i < m_tabManager->count(); ++i) {
        if (auto* split = qobject_cast<SplitTabWidget*>(m_tabManager->widget(i))) {
            if (split->primaryPage())
                split->primaryPage()->applySort(column, order);
            if (split->secondaryPage())
                split->secondaryPage()->applySort(column, order);
        }
    }
}

void MainWindow::updateSplitActions() {
    const bool split = m_tabManager->isSplit();

    if (m_syncSplitAction) {
        QSignalBlocker blocker(m_syncSplitAction);
        m_syncSplitAction->setEnabled(split);
        m_syncSplitAction->setChecked(split && m_tabManager->isSplitSyncEnabled());
    }
    if (m_copyToOtherPaneAction) {
        m_copyToOtherPaneAction->setEnabled(split);
    }
    if (m_moveToOtherPaneAction) {
        m_moveToOtherPaneAction->setEnabled(split);
    }
}

void MainWindow::applyIconSizeToAll(int size) {
    size = qBound(16, size, 256);
    AppSettings::instance().setIconSize(size);
    AppSettings::instance().save();

    for (int i = 0; i < m_tabManager->count(); ++i) {
        if (auto* split = qobject_cast<SplitTabWidget*>(m_tabManager->widget(i))) {
            if (split->primaryPage())
                split->primaryPage()->applyIconSize(size);
            if (split->secondaryPage())
                split->secondaryPage()->applyIconSize(size);
        }
    }
}

void MainWindow::adjustIconSize(int delta) {
    int current = AppSettings::instance().iconSize();
    if (TabPage* page = m_tabManager->currentPage()) {
        current = AppSettings::instance().viewOptions(page->currentPath()).iconSize;
    }
    applyIconSizeToAll(current + delta);
}

void MainWindow::applyChromeVisibility() {
    auto& settings = AppSettings::instance();
    m_sidebar->setVisible(settings.showSidebar());
    m_toolbar->setVisible(settings.showToolbar());
    m_toolbar->setPathBarVisible(settings.showPathBar());
    statusBar()->setVisible(settings.showStatusBar());
    if (m_showSidebarAction)
        m_showSidebarAction->setChecked(settings.showSidebar());
    if (m_showToolbarAction)
        m_showToolbarAction->setChecked(settings.showToolbar());
    if (m_showPathBarAction)
        m_showPathBarAction->setChecked(settings.showPathBar());
    if (m_showStatusBarAction)
        m_showStatusBarAction->setChecked(settings.showStatusBar());
}

void MainWindow::onSaveSearch() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    SmartFolderDialog dialog({}, page->currentSearchFilter(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    AppSettings::instance().saveSmartFolder(dialog.smartFolder());
    m_sidebar->refresh();
    page->showSmartFolder(dialog.smartFolder());
}

void MainWindow::onNewSmartFolder() {
    SearchFilter initial;
    if (TabPage* page = m_tabManager->currentPage()) {
        initial = page->currentSearchFilter();
        if (initial.query.isEmpty()) {
            const QString current = page->currentPath();
            initial.searchPath = (current.startsWith(QStringLiteral("smart://")) ||
                                  current.startsWith(QStringLiteral("tag://")))
                                     ? QDir::homePath()
                                     : current;
            initial.recursive = true;
        }
    }
    SmartFolderDialog dialog({}, initial, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    AppSettings::instance().saveSmartFolder(dialog.smartFolder());
    m_sidebar->refresh();
    if (TabPage* page = m_tabManager->currentPage()) {
        page->showSmartFolder(dialog.smartFolder());
    }
}

void MainWindow::onEditSmartFolder(const QString& name) {
    const SmartFolder folder = AppSettings::instance().smartFolder(name);
    if (!folder.isValid())
        return;

    SmartFolderDialog dialog(folder, folder.filter, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const SmartFolder updated = dialog.smartFolder();
    if (updated.name.compare(name, Qt::CaseInsensitive) != 0) {
        AppSettings::instance().removeSmartFolder(name);
    }
    AppSettings::instance().saveSmartFolder(updated);
    m_sidebar->refresh();

    if (TabPage* page = m_tabManager->currentPage()) {
        if (page->currentPath() == QStringLiteral("smart://") + name) {
            page->showSmartFolder(updated);
        }
    }
}

void MainWindow::onDeleteSmartFolder(const QString& name) {
    if (QMessageBox::question(
            this, QStringLiteral("Delete Smart Folder"),
            QStringLiteral("Delete the Smart Folder \"%1\"?\n\nThis does not delete any files.")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    AppSettings::instance().removeSmartFolder(name);
    m_sidebar->refresh();
    if (TabPage* page = m_tabManager->currentPage()) {
        if (page->currentPath() == QStringLiteral("smart://") + name) {
            page->navigateTo(AppSettings::instance().defaultPath());
        }
    }
}

void MainWindow::mountDiskImage(const QString& path) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString mountPath;
    QString error;
    const bool ok = DiskImageManager::mountImage(path, &mountPath, nullptr, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::warning(
            this, QStringLiteral("Cannot Mount Disk Image"),
            error.isEmpty() ? QStringLiteral("The disk image could not be mounted.") : error);
        return;
    }

    m_sidebar->refresh();
    openPath(mountPath);
}

void MainWindow::createDiskImageFromFolder(const QString& folder) {
    const QFileInfo info(folder);
    if (!info.isDir()) {
        QMessageBox::warning(this, QStringLiteral("Create Disk Image"),
                             QStringLiteral("Select a folder first."));
        return;
    }

    const QString suggested =
        info.absoluteFilePath() + QStringLiteral("/") + info.fileName() + QStringLiteral(".iso");
    const QString output =
        QFileDialog::getSaveFileName(this, QStringLiteral("Create Disk Image"), suggested,
                                     QStringLiteral("ISO Images (*.iso);;All Files (*)"));
    if (output.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = DiskImageManager::createIso(folder, output, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::warning(
            this, QStringLiteral("Create Disk Image"),
            error.isEmpty() ? QStringLiteral("The disk image could not be created.") : error);
        return;
    }

    if (TabPage* page = m_tabManager->currentPage())
        page->refresh(QFileInfo(output).fileName());
    m_sidebar->refresh();
    QMessageBox::information(this, QStringLiteral("Disk Image Created"),
                             QStringLiteral("Created %1").arg(output));
}

void MainWindow::onViewOptions() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    const QString folder = page->currentPath();
    ViewOptionsDialog dialog(folder, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    AppSettings::instance().setViewOptions(folder, dialog.options());
    AppSettings::instance().setListVisibleColumns(dialog.visibleColumns());
    for (int i = 0; i < m_tabManager->count(); ++i) {
        if (auto* split = qobject_cast<SplitTabWidget*>(m_tabManager->widget(i))) {
            if (split->primaryPage())
                split->primaryPage()->applyViewOptions();
            if (split->secondaryPage())
                split->secondaryPage()->applyViewOptions();
        }
    }
}

void MainWindow::onPreferences() {
    auto& settings = AppSettings::instance();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Preferences"));
    dialog.setMinimumWidth(420);

    auto* layout = new QVBoxLayout(&dialog);

    auto* restoreTabs = new QCheckBox(QStringLiteral("Reopen tabs from the last session"), &dialog);
    restoreTabs->setChecked(settings.restoreTabsOnStartup());
    layout->addWidget(restoreTabs);

    auto* dirsFirst = new QCheckBox(QStringLiteral("Keep folders on top when sorting"), &dialog);
    dirsFirst->setChecked(settings.directoriesFirst());
    layout->addWidget(dirsFirst);

    auto* rememberFolderViews =
        new QCheckBox(QStringLiteral("Remember view mode for each folder"), &dialog);
    rememberFolderViews->setChecked(settings.rememberFolderViewModes());
    layout->addWidget(rememberFolderViews);

    auto* calcFolderSizes =
        new QCheckBox(QStringLiteral("Calculate folder sizes in file lists"), &dialog);
    calcFolderSizes->setChecked(settings.calculateFolderSizes());
    layout->addWidget(calcFolderSizes);

    auto* springLoaded = new QCheckBox(QStringLiteral("Enable spring-loaded folders"), &dialog);
    springLoaded->setChecked(settings.springLoadedFoldersEnabled());
    layout->addWidget(springLoaded);

    auto* form = new QFormLayout();
    auto* springDelay = new QSpinBox(&dialog);
    springDelay->setRange(100, 3000);
    springDelay->setSingleStep(50);
    springDelay->setSuffix(QStringLiteral(" ms"));
    springDelay->setValue(settings.springLoadDelayMs());
    form->addRow(QStringLiteral("Spring-load delay:"), springDelay);

    auto* iconSize = new QSpinBox(&dialog);
    iconSize->setRange(16, 256);
    iconSize->setSingleStep(8);
    iconSize->setSuffix(QStringLiteral(" px"));
    iconSize->setValue(settings.iconSize());
    form->addRow(QStringLiteral("Icon size:"), iconSize);

    auto* trashPurgeDays = new QSpinBox(&dialog);
    trashPurgeDays->setRange(0, 365);
    trashPurgeDays->setSingleStep(1);
    trashPurgeDays->setSpecialValueText(QStringLiteral("Never"));
    trashPurgeDays->setSuffix(QStringLiteral(" days"));
    trashPurgeDays->setValue(settings.trashAutoPurgeDays());
    form->addRow(QStringLiteral("Auto-purge Trash older than:"), trashPurgeDays);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* defaultFileManagerBtn =
        new QPushButton(QStringLiteral("Set File Manager as the default file manager"), &dialog);
    layout->addWidget(defaultFileManagerBtn);
    connect(defaultFileManagerBtn, &QPushButton::clicked, this, [this]() {
        bool registered = false;

        QProcess xdg;
        xdg.start(QStringLiteral("xdg-mime"),
                  {QStringLiteral("default"), QStringLiteral("org.finderlinux.Finder.desktop"),
                   QStringLiteral("inode/directory")});
        if (xdg.waitForFinished(2000) && xdg.exitCode() == 0) {
            registered = true;
        }

        QProcess gio;
        gio.start(QStringLiteral("gio"), {QStringLiteral("mime"), QStringLiteral("inode/directory"),
                                          QStringLiteral("org.finderlinux.Finder.desktop")});
        if (gio.waitForFinished(2000) && gio.exitCode() == 0) {
            registered = true;
        }

        // Also install the FileManager1 and Application activation files so
        // browsers and launchers can start File Manager when it is not running yet.
        if (FileManager1Service::installActivationFile()) {
            registered = true;
        }
        if (ApplicationService::installActivationFile()) {
            registered = true;
        }

        QProcess::startDetached(QStringLiteral("kbuildsycoca6"),
                                {QStringLiteral("--noincremental")});

        QMessageBox::information(
            this, QStringLiteral("Default File Manager"),
            registered
                ? QStringLiteral(
                      "File Manager is now registered as the default file manager for folders.\n\n"
                      "If another file manager was handling \"Show in folder\", restarting "
                      "the session (or that file manager) once will hand it over to File Manager.")
                : QStringLiteral("Could not register File Manager as the default file manager."));
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    settings.setRestoreTabsOnStartup(restoreTabs->isChecked());
    settings.setSpringLoadedFoldersEnabled(springLoaded->isChecked());
    settings.setSpringLoadDelayMs(springDelay->value());
    settings.setDirectoriesFirst(dirsFirst->isChecked());
    settings.setRememberFolderViewModes(rememberFolderViews->isChecked());
    settings.setCalculateFolderSizes(calcFolderSizes->isChecked());
    settings.setTrashAutoPurgeDays(trashPurgeDays->value());
    settings.save();

    m_tabManager->setCalculateFolderSizes(calcFolderSizes->isChecked());

    applyIconSizeToAll(iconSize->value());

    for (int i = 0; i < m_tabManager->count(); ++i) {
        if (auto* split = qobject_cast<SplitTabWidget*>(m_tabManager->widget(i))) {
            if (split->primaryPage())
                split->primaryPage()->setDirectoriesFirst(dirsFirst->isChecked());
            if (split->secondaryPage())
                split->secondaryPage()->setDirectoriesFirst(dirsFirst->isChecked());
        }
    }
}

void MainWindow::onBatchRename() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        const QString single = page->selectedFilePath();
        if (!single.isEmpty())
            paths.append(single);
    }
    if (paths.isEmpty())
        return;
    if (!ensureOperationsIdle())
        return;

    BatchRenameDialog dialog(paths, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const auto renames = dialog.renames();
    if (renames.isEmpty())
        return;

    int renamed = 0;
    QStringList errors;
    for (const auto& pair : renames) {
        QString renamedPath;
        QString error;
        const QString newName = QFileInfo(pair.second).fileName();
        if (m_opEngine->renameItem(pair.first, newName, &renamedPath, &error)) {
            renamed++;
            UndoAction action;
            action.type = UndoActionType::Rename;
            action.description =
                QStringLiteral("Rename \"%1\"").arg(QFileInfo(pair.first).fileName());
            action.sourcePath = pair.first;
            action.targetPath = renamedPath;
            m_undoManager->pushAction(action);
        } else {
            errors.append(QStringLiteral("%1: %2").arg(QFileInfo(pair.first).fileName(), error));
        }
    }

    page->refresh();

    if (!errors.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Batch Rename"),
                             QStringLiteral("%1 item(s) were renamed.\n\n%2")
                                 .arg(renamed)
                                 .arg(QStringList(errors.mid(0, 10)).join(QLatin1Char('\n'))));
    }
}

void MainWindow::pasteIntoDirectory(const QString& destDir) {
    // Prefer the system clipboard so files copied in Dolphin, Nautilus or any
    // other application can be pasted here (and vice versa).
    const ClipboardFiles clip = readFileClipboard();
    QStringList sources = clip.valid ? clip.paths : m_clipboardPaths;
    const bool cut = clip.valid ? clip.cut : m_isCutOperation;

    sources = existingPathsOnly(sources);
    if (sources.isEmpty() || destDir.isEmpty())
        return;

    if (cut) {
        // Only forget the cut selection once the engine has accepted the job.
        if (m_opEngine->moveItems(sources, destDir)) {
            m_clipboardPaths.clear();
            m_isCutOperation = false;
        }
    } else {
        m_opEngine->copyItems(sources, destDir);
    }
}

void MainWindow::onDuplicate() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    QStringList paths = page->selectedFilePaths();
    if (!paths.isEmpty()) {
        m_opEngine->duplicateItems(paths);
    }
}

void MainWindow::onCompress(const QString& ext) {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty())
        return;

    QString format = ext.isEmpty() ? QStringLiteral("zip") : ext;
    QString destArchive = ArchiveService::suggestedArchiveName(paths, format);
    m_opEngine->compressItems(paths, destArchive);
}

void MainWindow::onCompressWithOptions() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    const QStringList paths = existingPathsOnly(page->selectedFilePaths());
    if (paths.isEmpty())
        return;

    CompressDialog dialog(paths, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString archivePath = dialog.archivePath();
    if (archivePath.isEmpty())
        return;

    m_opEngine->compressItems(paths, archivePath, dialog.compressionLevel(), dialog.passphrase());
}

void MainWindow::onExtractHere() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty())
        return;

    for (const auto& p : paths) {
        if (ArchiveService::isArchive(p)) {
            QString destDir = QFileInfo(p).absolutePath();
            m_opEngine->extractArchive(p, destDir);
        }
    }
}

void MainWindow::onExtractToFolder() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;
    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty())
        return;

    for (const auto& p : paths) {
        if (ArchiveService::isArchive(p)) {
            QString destDir = ArchiveService::suggestedExtractDir(p);
            m_opEngine->extractArchive(p, destDir);
        }
    }
}

void MainWindow::onExtractWithPassword() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList archives;
    for (const QString& path : page->selectedFilePaths()) {
        if (ArchiveService::isArchive(path)) {
            archives.append(path);
        }
    }
    if (archives.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Extract with Password"),
                                 QStringLiteral("Select one or more archive files to extract."));
        return;
    }

    bool ok = false;
    const QString password =
        QInputDialog::getText(this, QStringLiteral("Extract with Password"),
                              QStringLiteral("Password:"), QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;

    for (const QString& archive : archives) {
        m_opEngine->extractArchive(archive, QFileInfo(archive).absolutePath(), password);
    }
}

void MainWindow::onExtractAppImage() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList appImages;
    for (const QString& path : page->selectedFilePaths()) {
        if (DesktopAppsManager::isAppImageFile(path)) {
            appImages.append(path);
        }
    }
    if (appImages.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Extract AppImage"),
                                 QStringLiteral("Select one or more AppImage files to extract."));
        return;
    }

    auto* progress =
        new QProgressDialog(QStringLiteral("Extracting AppImage..."), QString(), 0, 0, this);
    progress->setWindowTitle(QStringLiteral("Extract AppImage"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->show();

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, progress]() {
        progress->close();
        progress->deleteLater();
        const QStringList errors = watcher->result();
        watcher->deleteLater();

        if (TabPage* current = m_tabManager->currentPage()) {
            current->refresh();
        }
        if (!errors.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Extract AppImage"),
                                 errors.join(QLatin1Char('\n')));
        }
    });

    watcher->setFuture(QtConcurrent::run([appImages]() {
        QStringList errors;
        for (const QString& appImage : appImages) {
            QString error;
            if (!ArchiveService::extractAppImage(appImage, nullptr, &error)) {
                errors.append(error);
            }
        }
        return errors;
    }));
}

void MainWindow::onMoveToTrash() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty())
        return;

    m_opEngine->trashItems(paths);
}

void MainWindow::onDeletePermanently() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty())
        return;

    auto btn = QMessageBox::warning(this, QStringLiteral("Permanent Delete"),
                                    QStringLiteral("Are you sure you want to permanently delete "
                                                   "these %1 items?\nThis action cannot be undone.")
                                        .arg(paths.size()),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (btn == QMessageBox::Yes) {
        m_opEngine->deletePermanently(paths);
    }
}

void MainWindow::onEmptyTrash() {
    auto btn = QMessageBox::warning(
        this, QStringLiteral("Empty Trash"),
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

void MainWindow::onPutBack() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty())
            paths.append(single);
    }
    if (paths.isEmpty())
        return;
    if (!ensureOperationsIdle())
        return;

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
        QMessageBox::warning(
            this, QStringLiteral("Restore Error"),
            failed == 1
                ? lastErr
                : QStringLiteral("Failed to restore %1 items: %2").arg(failed).arg(lastErr));
    }

    page->refresh();
    m_sidebar->refresh();
}

void MainWindow::onFilesDropped(const QStringList& sources, const QString& targetDir,
                                Qt::DropAction action) {
    if (sources.isEmpty() || targetDir.isEmpty())
        return;

    // Check if target is trash
    if (targetDir == XdgTrash::trashFilesDirectory() ||
        targetDir.startsWith(XdgTrash::trashDirectory())) {
        m_opEngine->trashItems(sources);
        return;
    }

    bool isRightClickOrAlt = (QGuiApplication::mouseButtons() & Qt::RightButton) ||
                             (QGuiApplication::keyboardModifiers() & Qt::AltModifier);

    if (isRightClickOrAlt) {
        QMenu dropMenu(this);
        QAction* copyAct = dropMenu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                                              QStringLiteral("Copy Here"));
        QAction* moveAct = dropMenu.addAction(QIcon::fromTheme(QStringLiteral("go-jump")),
                                              QStringLiteral("Move Here"));
        QAction* linkAct = dropMenu.addAction(QIcon::fromTheme(QStringLiteral("insert-link")),
                                              QStringLiteral("Link Here"));
        dropMenu.addSeparator();
        dropMenu.addAction(QStringLiteral("Cancel"));

        QAction* selected = dropMenu.exec(QCursor::pos());
        if (!selected || selected->text() == QStringLiteral("Cancel")) {
            return;
        }
        if (selected == copyAct) {
            action = Qt::CopyAction;
        } else if (selected == moveAct) {
            action = Qt::MoveAction;
        } else if (selected == linkAct) {
            for (const auto& src : sources) {
                QFileInfo fi(src);
                QString linkDst = QDir(targetDir).filePath(fi.fileName());
                QString err;
                m_opEngine->createSymlink(src, linkDst, &err);
            }
            if (TabPage* page = m_tabManager->currentPage()) {
                page->refresh();
            }
            return;
        }
    } else if (action != Qt::CopyAction &&
               !(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        // Cross-filesystem drops default to Copy instead of Move
        struct stat stSrc{}, stDst{};
        if (::stat(QFile::encodeName(sources.first()).constData(), &stSrc) == 0 &&
            ::stat(QFile::encodeName(targetDir).constData(), &stDst) == 0) {
            if (stSrc.st_dev != stDst.st_dev) {
                action = Qt::CopyAction;
            }
        }
    }

    if (action == Qt::CopyAction) {
        m_opEngine->copyItems(sources, targetDir);
    } else {
        m_opEngine->moveItems(sources, targetDir);
    }
}

void MainWindow::onQuickLook() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        QString single = page->selectedFilePath();
        if (!single.isEmpty())
            paths.append(single);
    }

    if (!paths.isEmpty()) {
        m_quickLookDialog->setFiles(paths);
        m_quickLookDialog->show();
        m_quickLookDialog->raise();
        m_quickLookDialog->activateWindow();
    }
}

void MainWindow::onGetInfo() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QStringList paths = page->selectedFilePaths();
    if (paths.isEmpty()) {
        const QString path = page->selectedFilePath();
        if (!path.isEmpty())
            paths.append(path);
    }
    if (paths.isEmpty()) {
        paths.append(page->currentPath());
    }

    auto* dlg = new GetInfoDialog(paths, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onToggleHidden() {
    bool show = !AppSettings::instance().showHiddenFiles();
    AppSettings::instance().setShowHiddenFiles(show);
    m_toolbar->setShowHidden(show);
    if (m_toggleHiddenAction) {
        m_toggleHiddenAction->setChecked(show);
    }
    m_tabManager->setShowHidden(show);
}

void MainWindow::onOpenTerminal() {
    TabPage* page = m_tabManager->currentPage();
    if (!page)
        return;

    QString error;
    if (!DesktopAppsManager::openTerminal(page->currentPath(), &error)) {
        QMessageBox::information(this, QStringLiteral("Terminal"), error);
    }
}

void MainWindow::onUndo() {
    if (!ensureOperationsIdle())
        return;
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

void MainWindow::onRedo() {
    if (!ensureOperationsIdle())
        return;
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

void MainWindow::onSearchTextChanged(const QString& query) {
    if (TabPage* page = m_tabManager->currentPage()) {
        page->setFilterQuery(query);
    }
}

void MainWindow::onOperationStarted(OperationType type, int totalItems, qint64 totalBytes) {
    m_operationStartTime = QDateTime::currentMSecsSinceEpoch();
    m_opEngine->resume();
    m_progressDialog->setOperation(type, totalItems, totalBytes);
}

void MainWindow::onOperationProgress(const OperationProgress& progress) {
    m_progressDialog->updateProgress(progress);
}

void MainWindow::onOperationConflict(const OperationConflict& conflict, ConflictResolution& res,
                                     bool& applyToAll) {
    ConflictDialog dlg(conflict, this);
    if (dlg.exec() == QDialog::Accepted) {
        res = dlg.resolution();
        applyToAll = dlg.applyToAll();
    } else {
        res = ConflictResolution::Cancel;
        applyToAll = false;
    }
}

void MainWindow::onOperationFinished(const OperationResult& result) {
    if (m_opEngine->activeAndPendingJobCount() == 0) {
        m_progressDialog->hide();
    }
    QString pathToSelect;
    if (result.success && !result.createdPaths.isEmpty()) {
        pathToSelect = result.createdPaths.first();
    }
    if (TabPage* page = m_tabManager->currentPage()) {
        if (!QDir(page->currentPath()).exists()) {
            page->goUp();
        }
    }
    m_tabManager->refreshCurrentTab(pathToSelect);
    m_sidebar->refresh();
    if (TabPage* page = m_tabManager->currentPage()) {
        m_sidebar->selectPath(page->currentPath());
    }
    updateStatusInfo();

    for (const auto& pair : result.movedPairs) {
        FileMetadataManager::instance().renamePath(pair.first, pair.second);
    }
    for (const auto& pair : result.trashedPairs) {
        FileMetadataManager::instance().forgetPath(pair.first);
    }
    if (result.type != OperationType::Trash && result.type != OperationType::Delete) {
        FileMetadataManager::instance().registerPaths(result.createdPaths);
    }

    for (const auto& pair : result.movedPairs) {
        UndoAction act;
        act.type = UndoActionType::Move;
        act.sourcePath = pair.first;
        act.targetPath = pair.second;
        act.description = QStringLiteral("Move %1").arg(QFileInfo(pair.second).fileName());
        m_undoManager->pushAction(act);
    }

    for (const auto& pair : result.trashedPairs) {
        UndoAction act;
        act.type = UndoActionType::Trash;
        act.sourcePath = pair.first;
        act.targetPath = pair.second;
        act.description = QStringLiteral("Trash %1").arg(QFileInfo(pair.first).fileName());
        m_undoManager->pushAction(act);
    }

    if (!result.success && !result.errorMessage.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Operation Incomplete"), result.errorMessage);
    } else if (!result.warnings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Operation Completed"),
                                 result.warnings.join(QLatin1Char('\n')));
    }

    // Surface as desktop notification when window is inactive, for long-running operations (> 3s),
    // or errors
    qint64 durationMs = QDateTime::currentMSecsSinceEpoch() - m_operationStartTime;
    if (!isActiveWindow() || durationMs > 3000 || !result.success) {
        QString verb = QStringLiteral("Operation");
        switch (result.type) {
        case OperationType::Copy:
            verb = QStringLiteral("Copy");
            break;
        case OperationType::Move:
            verb = QStringLiteral("Move");
            break;
        case OperationType::Duplicate:
            verb = QStringLiteral("Duplicate");
            break;
        case OperationType::Trash:
            verb = QStringLiteral("Move to Trash");
            break;
        case OperationType::Delete:
            verb = QStringLiteral("Delete");
            break;
        case OperationType::Compress:
            verb = QStringLiteral("Compression");
            break;
        case OperationType::Extract:
            verb = QStringLiteral("Extraction");
            break;
        default:
            break;
        }

        const QString summary = result.success ? QStringLiteral("%1 complete").arg(verb)
                                               : QStringLiteral("%1 failed").arg(verb);

        QString body;
        if (result.success) {
            body = QStringLiteral("%1 item(s) processed").arg(result.processedCount);
            if (result.skippedCount > 0) {
                body += QStringLiteral(", %1 skipped").arg(result.skippedCount);
            }
        } else {
            body = result.errorMessage.isEmpty()
                       ? QStringLiteral("%1 item(s) failed").arg(result.errorCount)
                       : result.errorMessage.left(240);
        }

        DesktopNotifier::notify(summary, body);
    }
}

void MainWindow::onContextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                                        const QString& currentDir) {
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    // Keep menu labels compact: long filenames are middle-elided (extension
    // preserved) and the full name is shown as a tooltip.
    const QFontMetrics menuMetrics(menu.font());
    auto elidedMenuName = [menuMetrics](const QString& name) {
        QString elided = menuMetrics.elidedText(name, Qt::ElideMiddle, 300);
        if (elided == name) {
            return elided;
        }
        // Avoid a run of dots/dashes right next to the ellipsis.
        elided.replace(QStringLiteral(".…"), QStringLiteral("…"));
        elided.replace(QStringLiteral("…."), QStringLiteral("…"));
        elided.replace(QStringLiteral("-…"), QStringLiteral("…"));
        elided.replace(QStringLiteral("_…"), QStringLiteral("…"));
        return elided;
    };

    auto addServiceMenuActions = [this](QMenu* parentMenu, const QStringList& paths) {
        if (paths.isEmpty()) {
            return;
        }

        const QVector<ServiceMenuAction> actions = ServiceMenuManager::actionsFor(paths);
        if (actions.isEmpty()) {
            return;
        }

        auto* actionsMenu = parentMenu->addMenu(QIcon::fromTheme(QStringLiteral("system-run")),
                                                QStringLiteral("Actions"));
        QHash<QString, QMenu*> submenus;
        for (const auto& action : actions) {
            QMenu* target = actionsMenu;
            if (!action.submenu.isEmpty()) {
                if (!submenus.contains(action.submenu)) {
                    submenus.insert(action.submenu, actionsMenu->addMenu(action.submenu));
                }
                target = submenus.value(action.submenu);
            }

            const QIcon icon = QIcon::fromTheme(
                action.icon, QIcon::fromTheme(QStringLiteral("application-x-executable")));
            target->addAction(icon, action.name, this, [this, action, paths]() {
                if (!ServiceMenuManager::launch(action, paths)) {
                    QMessageBox::warning(
                        this, QStringLiteral("Action Failed"),
                        QStringLiteral("The action \"%1\" could not be started.").arg(action.name));
                }
            });
        }
    };

    bool inTrash = (currentDir == XdgTrash::trashFilesDirectory() ||
                    currentDir.startsWith(XdgTrash::trashDirectory()) ||
                    (!selectedPaths.isEmpty() &&
                     selectedPaths.first().startsWith(XdgTrash::trashFilesDirectory())));

    if (!selectedPaths.isEmpty()) {
        // --- SELECTION CONTEXT MENU ---
        QString primaryPath = selectedPaths.first();
        QFileInfo primaryFi(primaryPath);
        bool isDir = primaryFi.isDir();
        bool single = (selectedPaths.size() == 1);

        if (inTrash) {
            auto* restoreAct = menu.addAction(
                QIcon::fromTheme(QStringLiteral("edit-undo")),
                single ? QStringLiteral("Put Back")
                       : QStringLiteral("Put Back %1 Items").arg(selectedPaths.size()),
                this, &MainWindow::onPutBack);
            QFont rf = restoreAct->font();
            rf.setBold(true);
            restoreAct->setFont(rf);

            menu.addSeparator();
            menu.addAction(QStringLiteral("Quick Look"), this, &MainWindow::onQuickLook);

            menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")),
                           QStringLiteral("Get Info"), this, [this, selectedPaths]() {
                               auto* dlg = new GetInfoDialog(selectedPaths, this);
                               dlg->setAttribute(Qt::WA_DeleteOnClose);
                               dlg->show();
                           });

            menu.addAction(QStringLiteral("Copy Path"), this, &MainWindow::onCopyPath);
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                           QStringLiteral("Delete Immediately..."), this,
                           &MainWindow::onDeletePermanently);
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("trash-empty")),
                           QStringLiteral("Empty Trash..."), this, &MainWindow::onEmptyTrash);

            menu.exec(globalPos);
            return;
        }

        // 1. Open
        if (single) {
            auto* openAct =
                menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                               QStringLiteral("Open"), this, [this, primaryPath, isDir]() {
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
                menu.addAction(QStringLiteral("Open in New Tab"), this,
                               [this, primaryPath]() { m_tabManager->createTab(primaryPath); });
                menu.addAction(QStringLiteral("Open in New Window"), this, [primaryPath]() {
                    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                            {QStringLiteral("--new-window"), primaryPath});
                });

                // Let folders be opened with other applications too (IDEs,
                // other file managers, terminal-based tools, ...).
                QMimeDatabase folderMimeDb;
                const QString folderMime = folderMimeDb.mimeTypeForFile(primaryPath).name();
                QVector<AppEntry> folderApps;
                for (const auto& app : DesktopAppsManager::appsForMime(folderMime)) {
                    if (app.id != QStringLiteral("org.finderlinux.Finder.desktop") &&
                        app.id != QStringLiteral("finder-linux.desktop")) {
                        folderApps.append(app);
                    }
                }
                if (!folderApps.isEmpty()) {
                    auto* folderOpenWith =
                        menu.addMenu(QIcon::fromTheme(QStringLiteral("application-x-executable")),
                                     QStringLiteral("Open With"));
                    for (const auto& app : folderApps) {
                        folderOpenWith->addAction(
                            app.icon, app.name, this, [this, appId = app.id, primaryPath]() {
                                if (!DesktopAppsManager::launchApp(appId, primaryPath)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Open Folder"),
                                        QStringLiteral(
                                            "The selected application could not be launched."));
                                }
                            });
                    }
                }

                bool isMount = VolumeManager::isMountPoint(primaryPath);
                if (isMount && primaryPath != QStringLiteral("/")) {
                    menu.addAction(
                        QIcon::fromTheme(QStringLiteral("media-eject")),
                        QStringLiteral("Eject / Unmount"), this, [this, primaryPath]() {
                            QApplication::setOverrideCursor(Qt::WaitCursor);
                            auto* watcher = new QFutureWatcher<QString>(this);
                            connect(watcher, &QFutureWatcher<QString>::finished, this,
                                    [this, watcher, primaryPath]() {
                                        QApplication::restoreOverrideCursor();
                                        const QString err = watcher->result();
                                        watcher->deleteLater();
                                        if (!err.isEmpty()) {
                                            QMessageBox::warning(
                                                this, QStringLiteral("Cannot Eject"), err);
                                            return;
                                        }
                                        m_sidebar->refresh();
                                        auto* page = m_tabManager->currentPage();
                                        if (page) {
                                            if (page->currentPath().startsWith(primaryPath)) {
                                                page->navigateTo(QDir::homePath());
                                            } else {
                                                page->refresh();
                                            }
                                        }
                                    });
                            watcher->setFuture(QtConcurrent::run([primaryPath]() {
                                QString error;
                                VolumeManager::ejectVolume(primaryPath, &error);
                                return error;
                            }));
                        });
                }

                bool isPinned = AppSettings::instance().customFavorites().contains(primaryPath);
                if (isPinned) {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-remove")),
                                   QStringLiteral("Unpin from Sidebar"), this,
                                   [this, primaryPath]() {
                                       AppSettings::instance().removeFavorite(primaryPath);
                                       m_sidebar->refresh();
                                   });
                } else {
                    menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")),
                                   QStringLiteral("Pin to Sidebar"), this, [this, primaryPath]() {
                                       AppSettings::instance().addFavorite(primaryPath);
                                       m_sidebar->refresh();
                                   });
                }
            } else {
                // Open With submenu
                QMimeDatabase mimeDb;
                QString mime = mimeDb.mimeTypeForFile(primaryPath).name();
                auto apps = DesktopAppsManager::appsForMime(mime);

                auto* openWithMenu =
                    menu.addMenu(QIcon::fromTheme(QStringLiteral("application-x-executable")),
                                 QStringLiteral("Open With"));
                if (!apps.isEmpty()) {
                    for (const auto& app : apps) {
                        QString title = app.name;
                        if (app.isDefault) {
                            title += QStringLiteral(" (default)");
                        }
                        openWithMenu->addAction(
                            app.icon, title, this, [this, appId = app.id, primaryPath]() {
                                if (!DesktopAppsManager::launchApp(appId, primaryPath)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Open File"),
                                        QStringLiteral(
                                            "The selected application could not be launched."));
                                }
                            });
                    }
                    openWithMenu->addSeparator();
                }

                openWithMenu->addAction(
                    QStringLiteral("Other Application..."), this,
                    [this, mime, primaryFi, primaryPath]() {
                        AppChooserDialog dlg(mime, primaryFi.fileName(), this);
                        if (dlg.exec() == QDialog::Accepted) {
                            QString custom = dlg.customCommand();
                            if (!custom.isEmpty()) {
                                if (!DesktopAppsManager::launchCustomCommand(custom, primaryPath)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Open File"),
                                        QStringLiteral(
                                            "The custom command could not be launched."));
                                }
                                return;
                            }

                            QString chosen = dlg.selectedAppId();
                            if (!chosen.isEmpty()) {
                                if (!DesktopAppsManager::launchApp(chosen, primaryPath)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Open File"),
                                        QStringLiteral(
                                            "The selected application could not be launched."));
                                }
                                if (dlg.makeDefault() &&
                                    !DesktopAppsManager::setDefaultAppForMime(mime, chosen)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Set Default"),
                                        QStringLiteral(
                                            "The default application could not be updated."));
                                }
                            }
                        }
                    });
            }
        } else {
            // Multiple items
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")),
                           QStringLiteral("Open %1 Items").arg(selectedPaths.size()), this,
                           [this, selectedPaths]() {
                               for (const auto& p : selectedPaths) {
                                   if (QFileInfo(p).isDir()) {
                                       openPath(p);
                                   } else {
                                       DesktopAppsManager::launchFile(p);
                                   }
                               }
                           });

            // Open With for multi-selection when all items share one file type.
            QMimeDatabase mimeDb;
            QString commonMime;
            bool allFiles = true;
            for (const QString& path : selectedPaths) {
                const QFileInfo info(path);
                if (info.isDir()) {
                    allFiles = false;
                    break;
                }
                const QString mime =
                    mimeDb.mimeTypeForFile(path, QMimeDatabase::MatchDefault).name();
                if (commonMime.isEmpty()) {
                    commonMime = mime;
                } else if (mime != commonMime) {
                    commonMime.clear();
                    break;
                }
            }

            if (allFiles && !commonMime.isEmpty()) {
                const QVector<AppEntry> apps = DesktopAppsManager::appsForMime(commonMime);
                if (!apps.isEmpty()) {
                    auto* openWithMenu =
                        menu.addMenu(QIcon::fromTheme(QStringLiteral("application-x-executable")),
                                     QStringLiteral("Open With"));
                    for (const auto& app : apps) {
                        QString title = app.name;
                        if (app.isDefault) {
                            title += QStringLiteral(" (default)");
                        }
                        openWithMenu->addAction(
                            app.icon, title, this, [this, appId = app.id, selectedPaths]() {
                                if (!DesktopAppsManager::launchApp(appId, selectedPaths)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Open Files"),
                                        QStringLiteral(
                                            "The selected application could not be launched."));
                                }
                            });
                    }

                    openWithMenu->addSeparator();
                    openWithMenu->addAction(
                        QStringLiteral("Other Application..."), this,
                        [this, commonMime, selectedPaths]() {
                            AppChooserDialog dlg(
                                commonMime, QStringLiteral("%1 items").arg(selectedPaths.size()),
                                this);
                            if (dlg.exec() != QDialog::Accepted) {
                                return;
                            }

                            const QString custom = dlg.customCommand();
                            if (!custom.isEmpty()) {
                                if (!DesktopAppsManager::launchCustomCommand(custom,
                                                                             selectedPaths)) {
                                    QMessageBox::warning(
                                        this, QStringLiteral("Cannot Open Files"),
                                        QStringLiteral(
                                            "The custom command could not be launched."));
                                }
                                return;
                            }

                            const QString chosen = dlg.selectedAppId();
                            if (chosen.isEmpty()) {
                                return;
                            }
                            if (!DesktopAppsManager::launchApp(chosen, selectedPaths)) {
                                QMessageBox::warning(
                                    this, QStringLiteral("Cannot Open Files"),
                                    QStringLiteral(
                                        "The selected application could not be launched."));
                            }
                            if (dlg.makeDefault() &&
                                !DesktopAppsManager::setDefaultAppForMime(commonMime, chosen)) {
                                QMessageBox::warning(
                                    this, QStringLiteral("Cannot Set Default"),
                                    QStringLiteral(
                                        "The default application could not be updated."));
                            }
                        });
                }
            }
        }

        // Quick Look
        menu.addAction(QStringLiteral("Quick Look"), this, &MainWindow::onQuickLook);

        // KDE service menu actions (Dolphin-compatible)
        addServiceMenuActions(&menu, selectedPaths);

        menu.addSeparator();

        // Get Info
        menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")),
                       QStringLiteral("Get Info"), this, [this, selectedPaths]() {
                           auto* dlg = new GetInfoDialog(selectedPaths, this);
                           dlg->setAttribute(Qt::WA_DeleteOnClose);
                           dlg->show();
                       });

        bool hasFile = false;
        for (const QString& path : selectedPaths) {
            if (!QFileInfo(path).isDir()) {
                hasFile = true;
                break;
            }
        }
        if (hasFile) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-properties")),
                           selectedPaths.size() > 1 ? QStringLiteral("Calculate Checksums...")
                                                    : QStringLiteral("Calculate Checksum..."),
                           this, &MainWindow::onCalculateChecksum);
        }

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

            dotBtn->setStyleSheet(
                QStringLiteral("QToolButton {"
                               "  background-color: %1;"
                               "  border-radius: 9px;"
                               "  border: %2;"
                               "}"
                               "QToolButton:hover { opacity: 0.85; }")
                    .arg(t.color.name(), allHave ? QStringLiteral("2px solid #ffffff")
                                                 : QStringLiteral("1px solid rgba(0,0,0,0.25)")));

            QObject::connect(dotBtn, &QToolButton::clicked, &menu,
                             [&menu, selectedPaths, tagName = t.name]() {
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
            "QToolButton:hover { background-color: rgba(128, 128, 128, 0.45); color: #ffffff; }"));
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
        auto* tagsMenu =
            menu.addMenu(QIcon::fromTheme(QStringLiteral("tag")), QStringLiteral("Tags"));
        for (const auto& t : TagManager::standardTags()) {
            bool hasTag = false;
            for (const auto& sp : selectedPaths) {
                if (TagManager::instance().hasTag(sp, t.name)) {
                    hasTag = true;
                    break;
                }
            }
            auto* act = tagsMenu->addAction(TagManager::tagDotIcon(t.color, 14), t.name, this,
                                            [selectedPaths, tagName = t.name]() {
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
                const QString extractFolder =
                    QFileInfo(ArchiveService::suggestedExtractDir(primaryPath)).fileName();
                QAction* extractAction = menu.addAction(
                    QIcon::fromTheme(QStringLiteral("archive-extract")),
                    QStringLiteral("Extract to \"%1/\"").arg(elidedMenuName(extractFolder)), this,
                    &MainWindow::onExtractToFolder);
                extractAction->setToolTip(extractFolder);
            } else {
                menu.addAction(QIcon::fromTheme(QStringLiteral("archive-extract")),
                               QStringLiteral("Extract Each to Subfolder"), this,
                               &MainWindow::onExtractToFolder);
            }
            menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-password")),
                           QStringLiteral("Extract with Password..."), this,
                           &MainWindow::onExtractWithPassword);
        }

        bool anyAppImage = false;
        for (const QString& path : selectedPaths) {
            if (DesktopAppsManager::isAppImageFile(path)) {
                anyAppImage = true;
                break;
            }
        }
        if (anyAppImage) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("package-x-generic")),
                           QStringLiteral("Extract AppImage..."), this,
                           &MainWindow::onExtractAppImage);
        }

        if (!isMount) {
            QString compressTitle =
                single ? QStringLiteral("Compress \"%1\"").arg(elidedMenuName(primaryFi.fileName()))
                       : QStringLiteral("Compress %1 Items").arg(selectedPaths.size());
            QAction* compressAction =
                menu.addAction(QIcon::fromTheme(QStringLiteral("archive-insert")), compressTitle,
                               this, [this]() { onCompress(QStringLiteral("zip")); });
            if (single) {
                compressAction->setToolTip(primaryFi.fileName());
            }

            auto* compressAsMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("archive-insert")),
                                                QStringLiteral("Compress As"));
            compressAsMenu->addAction(QStringLiteral(".zip (ZIP Archive)"), this,
                                      [this]() { onCompress(QStringLiteral("zip")); });
            compressAsMenu->addAction(QStringLiteral(".tar.gz (GZip Tarball)"), this,
                                      [this]() { onCompress(QStringLiteral("tar.gz")); });
            compressAsMenu->addAction(QStringLiteral(".tar.xz (XZ Tarball)"), this,
                                      [this]() { onCompress(QStringLiteral("tar.xz")); });
            compressAsMenu->addAction(QStringLiteral(".7z (7-Zip Archive)"), this,
                                      [this]() { onCompress(QStringLiteral("7z")); });
            compressAsMenu->addSeparator();
            compressAsMenu->addAction(QStringLiteral("Compression Options..."), this,
                                      &MainWindow::onCompressWithOptions);
        }

        // Copy Path
        menu.addAction(QStringLiteral("Copy Path"), this, &MainWindow::onCopyPath);
        menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")),
                       QStringLiteral("New Folder with Selection..."), this,
                       &MainWindow::onNewFolderWithSelection);
        if (selectedPaths.size() > 1) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")),
                           QStringLiteral("Batch Rename..."), this, &MainWindow::onBatchRename);
        }

        menu.addSeparator();

        // Cut
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")), QStringLiteral("Cut"), this,
                       &MainWindow::onCut);

        // Copy
        QAction* copyAction = menu.addAction(
            QIcon::fromTheme(QStringLiteral("edit-copy")),
            single ? QStringLiteral("Copy \"%1\"").arg(elidedMenuName(primaryFi.fileName()))
                   : QStringLiteral("Copy %1 Items").arg(selectedPaths.size()),
            this, &MainWindow::onCopy);
        if (single) {
            copyAction->setToolTip(primaryFi.fileName());
        }

        // Copy To / Move To
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy To..."),
                       this, &MainWindow::onCopyTo);
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")), QStringLiteral("Move To..."),
                       this, &MainWindow::onMoveTo);
        if (m_tabManager->isSplit()) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")),
                           QStringLiteral("Copy to Other Pane"), this,
                           &MainWindow::onCopyToOtherPane);
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-cut")),
                           QStringLiteral("Move to Other Pane"), this,
                           &MainWindow::onMoveToOtherPane);
        }

        // If folder is selected and the clipboard has files, allow "Paste into Folder"
        const bool clipboardHasFiles = readFileClipboard().valid || !m_clipboardPaths.isEmpty();
        if (single && isDir && clipboardHasFiles) {
            QAction* pasteAction = menu.addAction(
                QIcon::fromTheme(QStringLiteral("edit-paste")),
                QStringLiteral("Paste into \"%1\"").arg(elidedMenuName(primaryFi.fileName())), this,
                [this, primaryPath]() { pasteIntoDirectory(primaryPath); });
            pasteAction->setToolTip(primaryFi.fileName());
        }

        // Move to Trash and Delete Permanently (suppressed for mounts)
        if (!isMount) {
            menu.addSeparator();
            menu.addAction(QIcon::fromTheme(QStringLiteral("user-trash")),
                           QStringLiteral("Move to Trash"), this, &MainWindow::onMoveToTrash);
            menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                           QStringLiteral("Delete Permanently"), this,
                           &MainWindow::onDeletePermanently);
        }

        // Open in Terminal: for folders use the folder itself; for files use
        // the file's parent directory.
        if (single) {
            menu.addSeparator();
            const QString terminalDirectory = isDir ? primaryPath : primaryFi.absolutePath();
            menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")),
                           QStringLiteral("Open in Terminal"), this, [terminalDirectory]() {
                               DesktopAppsManager::openTerminal(terminalDirectory);
                           });
        }

    } else {
        // --- BACKGROUND / EMPTY SPACE CONTEXT MENU ---
        if (inTrash) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("trash-empty")),
                           QStringLiteral("Empty Trash..."), this, &MainWindow::onEmptyTrash);
            menu.exec(globalPos);
            return;
        }

        menu.addAction(QIcon::fromTheme(QStringLiteral("folder-new")), QStringLiteral("New Folder"),
                       this, &MainWindow::onNewFolder);
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-new")),
                       QStringLiteral("New Text File..."), this, &MainWindow::onNewTextFile);

        menu.addSeparator();

        // Get Info for current directory
        menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-information")),
                       QStringLiteral("Get Info"), this, [this, currentDir]() {
                           auto* dlg = new GetInfoDialog(currentDir, this);
                           dlg->setAttribute(Qt::WA_DeleteOnClose);
                           dlg->show();
                       });

        // Pin / Unpin current directory to Sidebar
        bool isPinned = AppSettings::instance().customFavorites().contains(currentDir);
        if (isPinned) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-remove")),
                           QStringLiteral("Unpin Current Folder from Sidebar"), this,
                           [this, currentDir]() {
                               AppSettings::instance().removeFavorite(currentDir);
                               m_sidebar->refresh();
                           });
        } else {
            menu.addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")),
                           QStringLiteral("Pin Current Folder to Sidebar"), this,
                           [this, currentDir]() {
                               AppSettings::instance().addFavorite(currentDir);
                               m_sidebar->refresh();
                           });
        }

        // Paste. The system clipboard may be owned by another application
        // (Dolphin, Nautilus, Firefox, ...), so include its contents here too.
        const ClipboardFiles clipboard = readFileClipboard();
        const bool clipboardHasFiles = clipboard.valid || !m_clipboardPaths.isEmpty();
        const qsizetype clipboardCount =
            clipboard.valid ? clipboard.paths.size() : m_clipboardPaths.size();
        auto* pasteAct = menu.addAction(
            QIcon::fromTheme(QStringLiteral("edit-paste")),
            clipboardHasFiles ? QStringLiteral("Paste (%1 Item%2)")
                                    .arg(clipboardCount)
                                    .arg(clipboardCount == 1 ? QString() : QStringLiteral("s"))
                              : QStringLiteral("Paste"),
            this, &MainWindow::onPaste);
        pasteAct->setEnabled(clipboardHasFiles);

        // Select All
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-select-all")),
                       QStringLiteral("Select All"), this, [this]() {
                           if (TabPage* p = m_tabManager->currentPage()) {
                               p->selectAll();
                           }
                       });
        menu.addAction(QStringLiteral("Select by Pattern..."), this,
                       &MainWindow::onSelectByPattern);
        menu.addAction(QStringLiteral("Invert Selection"), this, &MainWindow::onInvertSelection);

        menu.addSeparator();

        // Sort By submenu
        auto* sortMenu = menu.addMenu(QStringLiteral("Sort By"));
        auto* sortGroup = new QActionGroup(sortMenu);
        sortGroup->setExclusive(true);
        TabPage* sortPage = m_tabManager->currentPage();
        const int activeColumn =
            sortPage ? sortPage->sortColumn() : AppSettings::instance().sortColumn();
        const Qt::SortOrder activeOrder =
            sortPage ? sortPage->sortOrder() : AppSettings::instance().sortOrder();
        auto addSortField = [&, this](const QString& label, int column) {
            QAction* action = sortMenu->addAction(label);
            action->setCheckable(true);
            action->setChecked(activeColumn == column);
            action->setData(column);
            sortGroup->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, column, activeOrder]() { applySort(column, activeOrder); });
        };
        addSortField(QStringLiteral("Name"), FileSystemModel::ColName);
        addSortField(QStringLiteral("Date Modified"), FileSystemModel::ColDateModified);
        addSortField(QStringLiteral("Size"), FileSystemModel::ColSize);
        addSortField(QStringLiteral("Kind"), FileSystemModel::ColKind);

        sortMenu->addSeparator();
        auto* orderGroup = new QActionGroup(sortMenu);
        orderGroup->setExclusive(true);
        auto* contextAscending = sortMenu->addAction(QStringLiteral("Ascending"));
        contextAscending->setCheckable(true);
        contextAscending->setChecked(activeOrder == Qt::AscendingOrder);
        orderGroup->addAction(contextAscending);
        connect(contextAscending, &QAction::triggered, this,
                [this, activeColumn]() { applySort(activeColumn, Qt::AscendingOrder); });
        auto* contextDescending = sortMenu->addAction(QStringLiteral("Descending"));
        contextDescending->setCheckable(true);
        contextDescending->setChecked(activeOrder == Qt::DescendingOrder);
        orderGroup->addAction(contextDescending);
        connect(contextDescending, &QAction::triggered, this,
                [this, activeColumn]() { applySort(activeColumn, Qt::DescendingOrder); });

        // Show Hidden Files (checkable)
        bool showHidden = AppSettings::instance().showHiddenFiles();
        auto* hiddenAct =
            menu.addAction(QStringLiteral("Show Hidden Files"), this, &MainWindow::onToggleHidden);
        hiddenAct->setCheckable(true);
        hiddenAct->setChecked(showHidden);

        menu.addSeparator();

        // Open in Terminal
        menu.addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")),
                       QStringLiteral("Open in Terminal"), this,
                       [currentDir]() { DesktopAppsManager::openTerminal(currentDir); });

        // Refresh
        menu.addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Refresh"),
                       this, [this]() {
                           if (TabPage* p = m_tabManager->currentPage()) {
                               p->refresh();
                           }
                       });
    }

    menu.exec(globalPos);
}
