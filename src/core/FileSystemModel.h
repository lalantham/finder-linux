#pragma once

#include "FileItem.h"
#include <QAbstractItemModel>
#include <QFileSystemWatcher>
#include <QDir>
#include <QVector>
#include <QMimeData>
#include <QUrl>
#include <memory>

class FileSystemModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColDateModified,
        ColSize,
        ColKind,
        ColumnCount
    };

    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        FileSizeRole,
        FileDateRole,
        FileKindRole,
        FileIsDirRole,
        FileIsSymLinkRole,
        FilePermissionsRole,
        FileOwnerRole,
        FileGroupRole,
        FileFormattedSizeRole
    };

    enum class SortField {
        Name,
        DateModified,
        Size,
        Kind
    };

    explicit FileSystemModel(QObject* parent = nullptr);
    ~FileSystemModel() override = default;

    void setRootPath(const QString& path);
    [[nodiscard]] const QString& rootPath() const noexcept { return m_rootPath; }

    void setShowHidden(bool show);
    [[nodiscard]] bool showHidden() const noexcept { return m_showHidden; }

    void setDirectoriesFirst(bool dirsFirst);
    [[nodiscard]] bool directoriesFirst() const noexcept { return m_directoriesFirst; }

    void refresh();

    // QAbstractItemModel interface
    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    [[nodiscard]] const FileItem* itemAt(int row) const;
    [[nodiscard]] const FileItem* itemForIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForPath(const QString& path) const;

signals:
    void rootPathChanged(const QString& newPath);
    void directoryLoaded(const QString& path, int itemCount);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory, Qt::DropAction action);

private slots:
    void onDirectoryChanged(const QString& path);

private:
    void populate();
    void sortInternal();

    QString m_rootPath;
    bool m_showHidden{false};
    bool m_directoriesFirst{true};
    int m_sortColumn{ColName};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};

    QVector<FileItem> m_items;
    QFileSystemWatcher m_watcher;
};
