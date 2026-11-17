#pragma once

#include "AppSettings.h"
#include "FileItemDelegate.h"
#include "FileSystemModel.h"
#include <QTreeView>

class QTimer;

class ListView : public QTreeView {
    Q_OBJECT

public:
    explicit ListView(QWidget* parent = nullptr);

    void setFileSystemModel(FileSystemModel* model);
    void applyViewOptions(const FolderViewOptions& options);
    void setTextSize(int size);
    void applyColumnConfiguration();

signals:
    void openPathRequested(const QString& path);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onItemActivated(const QModelIndex& index);
    void onSpringTimeout();
    void onModelSortChanged(int column, Qt::SortOrder order);

private:
    void resetSpringTimer();
    void showHeaderMenu(const QPoint& pos);
    void saveColumnSettings();
    void setColumnVisible(int column, bool visible);
    void syncSortIndicator(int column, Qt::SortOrder order);
    [[nodiscard]] int defaultColumnWidth(int column) const;

    FileSystemModel* m_fsModel{nullptr};
    FileItemDelegate* m_delegate{nullptr};
    QTimer* m_springTimer{nullptr};
    QTimer* m_layoutSaveTimer{nullptr};
    QString m_springHoverPath;
    bool m_applyingColumns{false};
    bool m_syncingSort{false};
};
