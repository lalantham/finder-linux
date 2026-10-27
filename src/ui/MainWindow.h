#pragma once

#include "Toolbar.h"
#include "Sidebar.h"
#include "TabManager.h"
#include "FileOperationEngine.h"
#include "UndoManager.h"
#include "ProgressDialog.h"
#include "QuickLookDialog.h"
#include <QMainWindow>
#include <QSplitter>
#include <QLabel>
#include <QStringList>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Navigation
    void onNavigateBack();
    void onNavigateForward();
    void onNavigateUp();
    void onPathChanged(const QString& path);
    void onViewModeChanged(ViewMode mode);
    void onActivePageChanged(TabPage* page);

    // File operations
    void onNewFolder();
    void onNewTextFile();
    void onRename();
    void onCopy();
    void onCut();
    void onPaste();
    void onDuplicate();
    void onMakeSymlink();
    void onCopyPath();
    void onMoveToTrash();
    void onDeletePermanently();
    void onEmptyTrash();
    void onPutBack();
    void onFilesDropped(const QStringList& sources, const QString& targetDir, Qt::DropAction action);
    void onContextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths, const QString& currentDir);

    // Inspection & Preview
    void onQuickLook();
    void onGetInfo();
    void onToggleHidden();
    void onOpenTerminal();

    // Undo / Redo
    void onUndo();
    void onRedo();

    // Search
    void onSearchTextChanged(const QString& query);

    // Operations Engine callbacks
    void onOperationStarted(OperationType type, int totalItems, qint64 totalBytes);
    void onOperationProgress(const OperationProgress& progress);
    void onOperationConflict(const OperationConflict& conflict, ConflictResolution& res, bool& applyToAll);
    void onOperationFinished(const OperationResult& result);

private:
    void setupUi();
    void setupMenusAndShortcuts();
    void updateNavigationState();
    void updateStatusInfo();

    QSplitter* m_mainSplitter{nullptr};
    Toolbar* m_toolbar{nullptr};
    Sidebar* m_sidebar{nullptr};
    TabManager* m_tabManager{nullptr};

    QLabel* m_statusItemCountLabel{nullptr};
    QLabel* m_statusDiskFreeLabel{nullptr};

    FileOperationEngine* m_opEngine{nullptr};
    UndoManager* m_undoManager{nullptr};
    ProgressDialog* m_progressDialog{nullptr};
    QuickLookDialog* m_quickLookDialog{nullptr};

    QStringList m_clipboardPaths;
    bool m_isCutOperation{false};
};
