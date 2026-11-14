#pragma once

#include "AppSettings.h"
#include "FileItemDelegate.h"
#include "FileSystemModel.h"
#include <QHash>
#include <QListView>
#include <QPoint>

class IconView : public QListView {
    Q_OBJECT

public:
    explicit IconView(QWidget* parent = nullptr);

    void setFileSystemModel(FileSystemModel* model);
    void setIconSizeValue(int size);
    void applyViewOptions(const FolderViewOptions& options);
    void setFolderPath(const QString& path);
    void setManualArrangement(bool enabled);
    void cleanUp();

signals:
    void openPathRequested(const QString& path);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onItemActivated(const QModelIndex& index);
    void onSpringTimeout();

private:
    void resetSpringTimer();
    void updateLayoutMetrics();
    void applyIconPositions();
    void saveIconPositions();
    void alignToGrid();

    FileSystemModel* m_fsModel{nullptr};
    FileItemDelegate* m_delegate{nullptr};
    QTimer* m_springTimer{nullptr};
    QString m_springHoverPath;
    QString m_folderPath;
    QHash<QString, QPoint> m_iconPositions;
    bool m_manualArrangement{false};
    bool m_applyingPositions{false};
};
