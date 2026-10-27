#pragma once

#include "FileSystemModel.h"
#include <QTreeView>

class ListView : public QTreeView {
    Q_OBJECT

public:
    explicit ListView(QWidget* parent = nullptr);

    void setFileSystemModel(FileSystemModel* model);

signals:
    void openPathRequested(const QString& path);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onItemActivated(const QModelIndex& index);
    void onHeaderClicked(int logicalIndex);

private:
    FileSystemModel* m_fsModel{nullptr};
};
