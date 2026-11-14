#pragma once

#include "FileSystemModel.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QVector>
#include <QWidget>

class ColumnItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ColumnItemDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

    void setSpringHoverPath(const QString& path) {
        m_springHoverPath = path;
    }
    [[nodiscard]] QString springHoverPath() const noexcept {
        return m_springHoverPath;
    }
    void setTextSize(int size) {
        m_textSize = size;
    }
    [[nodiscard]] int textSize() const noexcept {
        return m_textSize;
    }

private:
    QString m_springHoverPath;
    int m_textSize{10};
};

class ColumnBrowserList : public QListView {
    Q_OBJECT

public:
    explicit ColumnBrowserList(const QString& path, QWidget* parent = nullptr);
    [[nodiscard]] QString path() const {
        return m_path;
    }
    [[nodiscard]] FileSystemModel* model() const {
        return m_model;
    }
    [[nodiscard]] QStringList selectedPaths() const;
    void selectPathWhenLoaded(const QString& path, bool notifyPassive = false,
                              bool applyNow = true);
    void updateColumnWidth();
    void setWidthAnimating(bool animating);
    void setItemDelegateTextSize(int size);

    [[nodiscard]] int preferredWidth() const noexcept {
        return m_preferredWidth;
    }

signals:
    void itemSelected(const QString& path, bool isDir);
    void multipleItemsSelected(const QStringList& paths);
    void selectionCleared();
    void itemDoubleClicked(const QString& path, bool isDir);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void navigateLeftRequested();
    void navigateRightRequested();
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                              const QString& currentDir);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory,
                           Qt::DropAction action);
    void passivePathSelected(const QString& path);
    void preferredWidthChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onSelectionChanged();
    void onSpringTimeout();

private:
    void resetSpringTimer();
    void applyPendingSelection(bool afterLoad);

    QString m_path;
    FileSystemModel* m_model{nullptr};
    ColumnItemDelegate* m_delegate{nullptr};
    QTimer* m_springTimer{nullptr};
    QString m_springHoverPath;
    QString m_lastSelectionSignature;
    QString m_pendingSelectPath;
    bool m_pendingNotifyPassive{false};
    int m_preferredWidth{240};
    bool m_widthAnimating{false};
    bool m_resizing{false};
    bool m_userResized{false};
    int m_resizeStartWidth{0};
    int m_resizeStartGlobalX{0};
};

class ColumnPreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit ColumnPreviewPane(QWidget* parent = nullptr);
    void setItem(const QString& path);
    void setMultipleItems(const QStringList& paths);

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
    QWidget* m_tagsContainer{nullptr};
    QHBoxLayout* m_tagsLayout{nullptr};
    QPushButton* m_qlBtn{nullptr};
};

class ColumnView : public QScrollArea {
    Q_OBJECT

public:
    explicit ColumnView(QWidget* parent = nullptr);

    void setRootPath(const QString& path);
    void navigateToPath(const QString& path);
    void refresh(const QString& targetToSelect = QString());
    void setShowHidden(bool show);
    [[nodiscard]] bool showHidden() const noexcept {
        return m_showHidden;
    }
    void setTextSize(int size);
    void applySort(int column, Qt::SortOrder order);
    [[nodiscard]] int sortColumn() const noexcept {
        return m_sortColumn;
    }
    [[nodiscard]] Qt::SortOrder sortOrder() const noexcept {
        return m_sortOrder;
    }
    [[nodiscard]] QString rootPath() const {
        return m_rootPath;
    }
    [[nodiscard]] QString activeDirectory() const;
    [[nodiscard]] int activeColumnRowCount() const;
    [[nodiscard]] QString selectedPath() const {
        return m_selectedPath;
    }
    [[nodiscard]] QStringList selectedPaths() const;
    [[nodiscard]] QListView* activeView() const;
    void selectAll();
    bool triggerRename();

signals:
    void openPathRequested(const QString& path);
    void activeDirectoryChanged(const QString& path);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contentLoaded();
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths,
                              const QString& currentDir);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory,
                           Qt::DropAction action);
    void itemRenamed(const QString& oldPath, const QString& newPath);

private slots:
    void onColumnItemSelected(int columnIndex, const QString& path, bool isDir);
    void onColumnMultipleItemsSelected(int columnIndex, const QStringList& paths);
    void onColumnSelectionCleared(int columnIndex);

private:
    void addColumn(const QString& path, bool animate = true);
    void truncateColumnsAfter(int columnIndex);
    void scrollToRightAnimated();

    QString m_rootPath;
    QString m_selectedPath;
    QStringList m_selectedPaths;
    int m_activeColumnIndex{-1};
    bool m_showHidden{false};
    int m_textSize{10};
    bool m_isRefreshing{false};
    int m_sortColumn{FileSystemModel::ColName};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
    QWidget* m_container{nullptr};
    QHBoxLayout* m_layout{nullptr};
    QVector<ColumnBrowserList*> m_columns;
    ColumnPreviewPane* m_previewPane{nullptr};
};
