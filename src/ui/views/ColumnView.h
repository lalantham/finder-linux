#pragma once

#include "FileSystemModel.h"
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QListView>
#include <QVector>
#include <QLabel>
#include <QStyledItemDelegate>

class ColumnItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ColumnItemDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

class ColumnBrowserList : public QListView {
    Q_OBJECT

public:
    explicit ColumnBrowserList(const QString& path, QWidget* parent = nullptr);
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] FileSystemModel* model() const { return m_model; }

signals:
    void itemSelected(const QString& path, bool isDir);
    void itemDoubleClicked(const QString& path, bool isDir);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void navigateLeftRequested();
    void navigateRightRequested();
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory, Qt::DropAction action);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onSelectionChanged();

private:
    QString m_path;
    FileSystemModel* m_model{nullptr};
};

class ColumnPreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit ColumnPreviewPane(QWidget* parent = nullptr);
    void setItem(const QString& path);

signals:
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);

private:
    QString m_currentPath;
    QLabel* m_iconLabel{nullptr};
    QLabel* m_nameLabel{nullptr};
    QLabel* m_kindLabel{nullptr};
    QLabel* m_sizeLabel{nullptr};
    QLabel* m_modifiedLabel{nullptr};
};

class ColumnView : public QScrollArea {
    Q_OBJECT

public:
    explicit ColumnView(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    void refresh();
    [[nodiscard]] QString rootPath() const { return m_rootPath; }
    [[nodiscard]] QString selectedPath() const { return m_selectedPath; }
    [[nodiscard]] QStringList selectedPaths() const { return m_selectedPath.isEmpty() ? QStringList{} : QStringList{m_selectedPath}; }

signals:
    void openPathRequested(const QString& path);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory, Qt::DropAction action);

private slots:
    void onColumnItemSelected(int columnIndex, const QString& path, bool isDir);

private:
    void addColumn(const QString& path, bool animate = true);
    void truncateColumnsAfter(int columnIndex);
    void scrollToRightAnimated();

    QString m_rootPath;
    QString m_selectedPath;
    QWidget* m_container{nullptr};
    QHBoxLayout* m_layout{nullptr};
    QVector<ColumnBrowserList*> m_columns;
    ColumnPreviewPane* m_previewPane{nullptr};
};
