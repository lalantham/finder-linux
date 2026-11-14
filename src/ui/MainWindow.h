#pragma once

#include "FileOperationEngine.h"
#include "ProgressDialog.h"
#include "QuickLookDialog.h"
#include "Sidebar.h"
#include "TabManager.h"
#include "Toolbar.h"
#include "UndoManager.h"
#include <QLabel>
#include <QMainWindow>
#include <QSplitter>
#include <QStringList>
#include <QVariantList>

class TerminalPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString& path);
    void openInNewTab(const QString& path);
    void openAndSelect(const QString& filePath);
    void openRemoteLocation(const QString& uri);

    // Used by the org.freedesktop.FileManager1 D-Bus service (browsers call
    // these for "Show in folder" and "Properties").
    void showItems(const QStringList& paths);
    void showItemProperties(const QStringList& paths);

    // Persists tabs and window state. Also invoked by the session manager on
    // logout, so the next login restores the same session.
    void saveSessionState();

    // org.freedesktop.Application helpers.
    void activateFromDesktop();
    void setActivationToken(const QString& token);
    void activateDesktopAction(const QString& actionName, const QVariantList& parameter);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // Navigation
    void onNavigateBack();
    void onNavigateForward();
    void onNavigateUp();
    void onPathChanged(const QString& path);
    void onViewModeChanged(ViewMode mode);
    void onToggleSplitView();
    void onActivePageChanged(TabPage* page);

    // File operations
    void onNewFolder();
    void onNewTextFile();
    void onRename();
    void onCopy();
    void onCut();
    void onPaste();
    void onDuplicate();
    void onCompress(const QString& ext = QString());
    void onCompressWithOptions();
    void onExtractHere();
    void onExtractToFolder();
    void onExtractWithPassword();
    void onExtractAppImage();
    void onMakeSymlink();
    void onCopyPath();
    void onCopyTo();
    void onMoveTo();
    void onCopyToOtherPane();
    void onMoveToOtherPane();
    void onNewFolderWithSelection();
    void onSelectByPattern();
    void onInvertSelection();
    void onCalculateChecksum();
    void onMoveToTrash();
    void onDeletePermanently();
    void onEmptyTrash();
    void onPutBack();
    void onFilesDropped(const QStringList& sources, const QString& targetDir,
                        Qt::DropAction action);
    void onContextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                                const QString& currentDir);

    // Inspection & Preview
    void onQuickLook();
    void onGetInfo();
    void onToggleHidden();
    void onOpenTerminal();
    void onViewOptions();
    void onSaveSearch();
    void onNewSmartFolder();
    void onEditSmartFolder(const QString& name);
    void onDeleteSmartFolder(const QString& name);

    // Undo / Redo
    void onUndo();
    void onRedo();

    // Search
    void onSearchTextChanged(const QString& query);

    // Operations Engine callbacks
    void onOperationStarted(OperationType type, int totalItems, qint64 totalBytes);
    void onOperationProgress(const OperationProgress& progress);
    void onOperationConflict(const OperationConflict& conflict, ConflictResolution& res,
                             bool& applyToAll);
    void onOperationFinished(const OperationResult& result);

private:
    void setupUi();
    void setupMenusAndShortcuts();
    void updateNavigationState();
    void updateStatusInfo();
    bool ensureOperationsIdle();
    void pasteIntoDirectory(const QString& destDir);
    void connectToServerAsync(const QString& uri);
    void applySort(int column, Qt::SortOrder order);
    void updateSplitActions();
    void applyIconSizeToAll(int size);
    void adjustIconSize(int delta);
    void applyChromeVisibility();
    void onPreferences();
    void onBatchRename();
    void toggleTerminalPanel();
    void requestKWinActivation();

    QSplitter* m_verticalSplitter{nullptr};
    QSplitter* m_mainSplitter{nullptr};
    Toolbar* m_toolbar{nullptr};
    Sidebar* m_sidebar{nullptr};
    TabManager* m_tabManager{nullptr};
    TerminalPanel* m_terminalPanel{nullptr};

    QLabel* m_statusItemCountLabel{nullptr};
    QLabel* m_statusDiskFreeLabel{nullptr};

    FileOperationEngine* m_opEngine{nullptr};
    UndoManager* m_undoManager{nullptr};
    ProgressDialog* m_progressDialog{nullptr};
    QuickLookDialog* m_quickLookDialog{nullptr};
    QAction* m_toggleHiddenAction{nullptr};
    QAction* m_backAction{nullptr};
    QAction* m_fwdAction{nullptr};
    QAction* m_syncSplitAction{nullptr};
    QAction* m_copyToOtherPaneAction{nullptr};
    QAction* m_moveToOtherPaneAction{nullptr};
    QAction* m_showSidebarAction{nullptr};
    QAction* m_showToolbarAction{nullptr};
    QAction* m_showPathBarAction{nullptr};
    QAction* m_showStatusBarAction{nullptr};
    QAction* m_fullScreenAction{nullptr};

    QStringList m_clipboardPaths;
    bool m_isCutOperation{false};
    bool m_kwinActivationPending{false};
    qint64 m_operationStartTime{0};
};
