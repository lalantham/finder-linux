#pragma once

#include "FileSystemModel.h"
#include "FileItemDelegate.h"
#include <QListView>

class IconView : public QListView {
    Q_OBJECT

public:
    explicit IconView(QWidget* parent = nullptr);

    void setFileSystemModel(FileSystemModel* model);
    void setIconSizeValue(int size);

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

private:
    FileSystemModel* m_fsModel{nullptr};
    FileItemDelegate* m_delegate{nullptr};
};
