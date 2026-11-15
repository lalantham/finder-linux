#pragma once

#include "FileItem.h"
#include <QAbstractItemModel>
#include <QDir>
#include <QFileSystemWatcher>
#include <QMimeData>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <atomic>
#include <memory>

class QTimer;

class FileSystemModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColDateModified,
        ColSize,
        ColKind,
        // Additional metadata columns. Existing values are kept stable so
        // saved sort settings keep working across upgrades.
        ColDateCreated,
        ColDateOpened,
        ColTags,
        ColPermissions,
        ColOwner,
        ColGroup,
        ColComment,
        ColDateAdded,
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
        FileFormattedSizeRole,
        FileTagsRole,
        FileDateCreatedRole,
        FileDateOpenedRole,
        FileCommentRole,
        FileMimeTypeRole,
        FileDateAddedRole,
        FileRelevanceRole
    };

    enum class SortField {
        Name,
        DateModified,
        DateCreated,
        DateOpened,
        Size,
        Kind,
        Tags,
        Permissions,
        Owner,
        Group,
        Comment,
        DateAdded
    };

    // Stable identifiers used by the list view and persisted settings.
    [[nodiscard]] static QString columnKey(int column);
    [[nodiscard]] static int columnForKey(const QString& key);
    [[nodiscard]] static QString columnTitle(int column);
    [[nodiscard]] static QList<int> allColumns();

    explicit FileSystemModel(QObject* parent = nullptr);
    ~FileSystemModel() override;

    // When enabled, directory scans run in the background and the model is
    // reset once the result is available. Disabled by default so synchronous
    // callers/tests keep their immediate-population semantics.
    static void setAsyncLoadingEnabled(bool enabled);
    [[nodiscard]] static bool asyncLoadingEnabled();

    void setRootPath(const QString& path);
    void setTaggedFiles(const QString& tag);
    [[nodiscard]] bool isTaggedView() const noexcept {
        return !m_activeTag.isEmpty();
    }
    [[nodiscard]] const QString& activeTag() const noexcept {
        return m_activeTag;
    }
    [[nodiscard]] const QString& rootPath() const noexcept {
        return m_rootPath;
    }

    void setFilterQuery(const QString& query, const QString& kind = QString());
    [[nodiscard]] const QString& filterQuery() const noexcept {
        return m_filterQuery;
    }
    [[nodiscard]] const QString& filterKind() const noexcept {
        return m_filterKind;
    }

    void setSearchResults(const QVector<FileItem>& items, bool applySorting = true);
    void appendSearchResults(const QVector<FileItem>& items);
    void setSearchResultsFilter(const QString& text);
    [[nodiscard]] QString searchResultsFilter() const noexcept {
        return m_searchResultsFilter;
    }
    void setSortByRelevance(bool enabled);
    [[nodiscard]] bool sortByRelevance() const noexcept {
        return m_sortByRelevance;
    }
    void clearSearchResults();
    [[nodiscard]] bool isSearchResultsView() const noexcept {
        return m_isSearchResultsView;
    }
    void exitSearchResults();

    void setShowHidden(bool show);
    [[nodiscard]] bool showHidden() const noexcept {
        return m_showHidden;
    }

    void setDirectoriesFirst(bool dirsFirst);
    [[nodiscard]] bool directoriesFirst() const noexcept {
        return m_directoriesFirst;
    }

    void setCalculateFolderSizes(bool enabled);
    [[nodiscard]] bool calculateFolderSizes() const noexcept {
        return m_calculateFolderSizes;
    }

    void refresh();

    // QAbstractItemModel interface
    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    [[nodiscard]] Qt::DropActions supportedDropActions() const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                       int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    [[nodiscard]] int sortColumn() const noexcept {
        return m_sortColumn;
    }
    [[nodiscard]] Qt::SortOrder sortOrder() const noexcept {
        return m_sortOrder;
    }

    [[nodiscard]] const FileItem* itemAt(int row) const;
    [[nodiscard]] const FileItem* itemForIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForPath(const QString& path) const;
    [[nodiscard]] const QString& longestFileName() const noexcept {
        return m_longestFileName;
    }

signals:
    void rootPathChanged(const QString& newPath);
    void directoryLoading(const QString& path);
    void directoryLoaded(const QString& path, int itemCount);
    void sortChanged(int column, Qt::SortOrder order);
    void rootDirectoryChanged(const QString& path);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory,
                           Qt::DropAction action);
    void itemRenamed(const QString& oldPath, const QString& newPath);

private slots:
    void onDirectoryChanged(const QString& path);

private:
    void populate();
    void populateSync();
    void populateAsync();
    void cancelPendingLoad();
    static bool isLocalFileSystem(const QString& path);
    void sortInternal();
    void applyFilterInternal();
    void rebuildSearchResults();
    static bool itemMatchesFilter(const FileItem& item, const QString& query, const QString& kind);
    void scheduleFolderSizeCalculations();

    QString m_rootPath;
    QString m_activeTag;
    QString m_filterQuery;
    QString m_filterKind;
    bool m_isSearchResultsView{false};
    bool m_showHidden{false};
    bool m_directoriesFirst{true};
    bool m_calculateFolderSizes{false};
    int m_sortColumn{ColName};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};

    QVector<FileItem> m_items;
    QVector<FileItem> m_searchResultItems;
    QString m_searchResultsFilter;
    bool m_sortByRelevance{false};
    QVector<FileItem> m_allFolderItems;
    QHash<QString, QPair<qint64, int>> m_folderSizeCache;
    QHash<QString, qint64> m_folderSizeCacheTime;
    QString m_longestFileName;
    QFileSystemWatcher m_watcher;
    QTimer* m_refreshTimer{nullptr};

    static bool s_asyncLoading;
    quint64 m_generation{0};
    quint64 m_folderSizeGeneration{0};
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
};
