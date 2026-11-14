#include "FileSystemModel.h"
#include "FileOperationEngine.h"
#include "TagManager.h"
#include "AppSettings.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

bool FileSystemModel::s_asyncLoading = false;

void FileSystemModel::setAsyncLoadingEnabled(bool enabled)
{
    s_asyncLoading = enabled;
}

bool FileSystemModel::asyncLoadingEnabled()
{
    return s_asyncLoading;
}

FileSystemModel::FileSystemModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    m_calculateFolderSizes = AppSettings::instance().calculateFolderSizes();
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileSystemModel::onDirectoryChanged);

    // Coalesce bursts of filesystem events (unpacking an archive, downloads)
    // into a single directory rescan.
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(300);
    connect(m_refreshTimer, &QTimer::timeout, this, &FileSystemModel::populate);

    connect(&TagManager::instance(), &TagManager::tagsChanged, this, [this](const QString& path, const QStringList& /*tags*/) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].path() == path) {
                m_items[i].refresh();
                QModelIndex idx = index(i, 0);
                emit dataChanged(idx, index(i, columnCount() - 1), {FileTagsRole, Qt::DisplayRole});
                break;
            }
        }
    });
}

FileSystemModel::~FileSystemModel()
{
    ++m_generation;
    if (m_cancelFlag) {
        m_cancelFlag->store(true, std::memory_order_relaxed);
    }
}

void FileSystemModel::setRootPath(const QString& path)
{
    m_activeTag.clear();
    m_filterQuery.clear();
    m_filterKind.clear();
    m_isSearchResultsView = false;
    QString cleanPath = QDir::cleanPath(path);
    if (m_rootPath == cleanPath) return;

    if (!m_rootPath.isEmpty()) {
        m_watcher.removePath(m_rootPath);
    }

    m_rootPath = cleanPath;
    if (!m_rootPath.isEmpty() && QDir(m_rootPath).exists() && isLocalFileSystem(m_rootPath)) {
        m_watcher.addPath(m_rootPath);
    }

    populate();
    emit rootPathChanged(m_rootPath);
}

void FileSystemModel::setTaggedFiles(const QString& tag)
{
    m_filterQuery.clear();
    m_filterKind.clear();
    m_isSearchResultsView = false;
    if (!m_rootPath.isEmpty() && !m_rootPath.startsWith(QStringLiteral("tag://"))) {
        m_watcher.removePath(m_rootPath);
    }
    m_activeTag = tag;
    m_rootPath = QStringLiteral("tag://") + tag;
    populate();
    emit rootPathChanged(m_rootPath);
}

void FileSystemModel::setShowHidden(bool show)
{
    if (m_showHidden == show) return;
    m_showHidden = show;
    populate();
}

void FileSystemModel::setDirectoriesFirst(bool dirsFirst)
{
    if (m_directoriesFirst == dirsFirst) return;
    m_directoriesFirst = dirsFirst;
    sort(m_sortColumn, m_sortOrder);
}

void FileSystemModel::setCalculateFolderSizes(bool enabled)
{
    if (m_calculateFolderSizes == enabled) return;
    m_calculateFolderSizes = enabled;
    if (m_calculateFolderSizes) {
        scheduleFolderSizeCalculations();
    } else {
        refresh();
    }
}

void FileSystemModel::refresh()
{
    populate();
}

void FileSystemModel::onDirectoryChanged(const QString& path)
{
    if (path == m_rootPath && m_refreshTimer) {
        m_refreshTimer->start();
    }
}

bool FileSystemModel::isLocalFileSystem(const QString& path)
{
    if (path.startsWith(QStringLiteral("tag://"))) {
        return false;
    }
    if (path.contains(QStringLiteral("/gvfs/")) || path.contains(QStringLiteral("/kio-fuse/"))) {
        return false;
    }

    QStorageInfo storage(path);
    if (!storage.isValid()) {
        return false;
    }

    static const QSet<QString> remoteTypes = {
        QStringLiteral("nfs"), QStringLiteral("nfs4"), QStringLiteral("cifs"),
        QStringLiteral("smb3"), QStringLiteral("smbfs"), QStringLiteral("afs"),
        QStringLiteral("ceph"), QStringLiteral("9p"), QStringLiteral("davfs"),
        QStringLiteral("fuse.sshfs"), QStringLiteral("fuse.rclone"),
        QStringLiteral("fuse.gvfsd-fuse"), QStringLiteral("fuse.kio-fuse"),
        QStringLiteral("fuse.smbnetfs")
    };
    const QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();
    return !remoteTypes.contains(fsType);
}

void FileSystemModel::populate()
{
    // Tag collections are small and read the TagManager registry, which is not
    // worth touching from a worker during shutdown.
    if (s_asyncLoading && m_activeTag.isEmpty()) {
        populateAsync();
    } else {
        populateSync();
    }
}

void FileSystemModel::cancelPendingLoad()
{
    ++m_generation;
    if (m_cancelFlag) {
        m_cancelFlag->store(true, std::memory_order_relaxed);
        m_cancelFlag.reset();
    }
}

void FileSystemModel::populateSync()
{
    ++m_generation;
    if (m_cancelFlag) {
        m_cancelFlag->store(true, std::memory_order_relaxed);
        m_cancelFlag.reset();
    }

    beginResetModel();
    m_items.clear();
    m_allFolderItems.clear();

    if (!m_activeTag.isEmpty()) {
        QStringList paths = TagManager::instance().filesForTag(m_activeTag);
        m_allFolderItems.reserve(paths.size());
        for (const auto& p : paths) {
            if (QFileInfo::exists(p)) {
                m_allFolderItems.emplace_back(p);
            }
        }
        applyFilterInternal();
    } else if (!m_rootPath.isEmpty()) {
        QDir dir(m_rootPath);
        QDir::Filters filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System;
        if (m_showHidden) {
            filters |= QDir::Hidden;
        }

        QFileInfoList entries = dir.entryInfoList(filters, QDir::NoSort);
        m_allFolderItems.reserve(entries.size());
        for (const auto& fi : entries) {
            m_allFolderItems.emplace_back(fi);
        }

        applyFilterInternal();
    }

    endResetModel();
    emit directoryLoaded(m_rootPath, static_cast<int>(m_items.size()));
}

void FileSystemModel::populateAsync()
{
    const quint64 generation = ++m_generation;
    if (m_cancelFlag) {
        m_cancelFlag->store(true, std::memory_order_relaxed);
    }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_cancelFlag = cancel;

    const bool tagView = !m_activeTag.isEmpty();
    const QString tag = m_activeTag;
    const QString root = m_rootPath;
    const bool showHidden = m_showHidden;

    emit directoryLoading(root);

    // The watcher is owned by the model, so destroying the model cancels the
    // callback; the scan itself keeps no pointer to the model.
    auto* watcher = new QFutureWatcher<QVector<FileItem>>(this);
    connect(watcher, &QFutureWatcher<QVector<FileItem>>::finished, this, [this, watcher, generation]() {
        const QVector<FileItem> items = watcher->result();
        watcher->deleteLater();

        if (generation != m_generation) {
            return; // a newer scan superseded this one
        }

        beginResetModel();
        m_allFolderItems = items;
        applyFilterInternal();
        endResetModel();
        emit directoryLoaded(m_rootPath, static_cast<int>(m_items.size()));
    });

    watcher->setFuture(QtConcurrent::run([tagView, tag, root, showHidden, cancel]() {
        QVector<FileItem> items;
        if (cancel->load(std::memory_order_relaxed)) {
            return items;
        }

        if (tagView) {
            const QStringList paths = TagManager::instance().filesForTag(tag);
            items.reserve(paths.size());
            for (const auto& path : paths) {
                if (cancel->load(std::memory_order_relaxed)) break;
                if (QFileInfo::exists(path)) {
                    items.emplace_back(path);
                }
            }
        } else if (!root.isEmpty()) {
            QDir::Filters filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System;
            if (showHidden) {
                filters |= QDir::Hidden;
            }

            QDirIterator iterator(root, filters);
            while (iterator.hasNext()) {
                if (cancel->load(std::memory_order_relaxed)) break;
                iterator.next();
                items.emplace_back(iterator.fileInfo());
            }
        }

        return items;
    }));
}

bool FileSystemModel::itemMatchesFilter(const FileItem& item, const QString& query, const QString& kind)
{
    if (!query.isEmpty()) {
        bool matchesName = item.fileName().contains(query, Qt::CaseInsensitive);
        bool matchesTag = false;
        const QStringList tags = item.tags();
        for (const auto& t : tags) {
            if (t.contains(query, Qt::CaseInsensitive)) {
                matchesTag = true;
                break;
            }
        }
        if (!matchesName && !matchesTag) {
            return false;
        }
    }

    if (!kind.isEmpty()) {
        QString k = kind.toLower();
        QString ext = item.suffix().toLower();
        if (k == QStringLiteral("folders") || k == QStringLiteral("folder")) {
            if (!item.isDir()) return false;
        } else if (item.isDir()) {
            return false;
        } else if (k == QStringLiteral("images") || k == QStringLiteral("image")) {
            static const QSet<QString> imgExts = {
                QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
                QStringLiteral("bmp"), QStringLiteral("ico"), QStringLiteral("tiff"), QStringLiteral("tif")
            };
            if (!imgExts.contains(ext) && !item.kind().contains(QStringLiteral("image"), Qt::CaseInsensitive)) {
                return false;
            }
        } else if (k == QStringLiteral("documents") || k == QStringLiteral("document")) {
            static const QSet<QString> docExts = {
                QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"),
                QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("rtf"),
                QStringLiteral("odt"), QStringLiteral("xls"), QStringLiteral("xlsx"),
                QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("csv")
            };
            if (!docExts.contains(ext) &&
                !item.kind().contains(QStringLiteral("document"), Qt::CaseInsensitive) &&
                !item.kind().contains(QStringLiteral("text"), Qt::CaseInsensitive)) {
                return false;
            }
        } else if (k == QStringLiteral("videos") || k == QStringLiteral("video")) {
            static const QSet<QString> vidExts = {
                QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
                QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"),
                QStringLiteral("flv"), QStringLiteral("m4v")
            };
            if (!vidExts.contains(ext) && !item.kind().contains(QStringLiteral("video"), Qt::CaseInsensitive)) {
                return false;
            }
        } else if (k == QStringLiteral("audio")) {
            static const QSet<QString> audExts = {
                QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
                QStringLiteral("ogg"), QStringLiteral("m4a"), QStringLiteral("aac"),
                QStringLiteral("wma"), QStringLiteral("opus")
            };
            if (!audExts.contains(ext) && !item.kind().contains(QStringLiteral("audio"), Qt::CaseInsensitive)) {
                return false;
            }
        } else if (k == QStringLiteral("archives") || k == QStringLiteral("archive")) {
            static const QSet<QString> arcExts = {
                QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"),
                QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("7z"),
                QStringLiteral("rar"), QStringLiteral("tgz")
            };
            if (!arcExts.contains(ext) && !item.kind().contains(QStringLiteral("archive"), Qt::CaseInsensitive)) {
                return false;
            }
        }
    }

    return true;
}

void FileSystemModel::applyFilterInternal()
{
    if (m_filterQuery.isEmpty() && m_filterKind.isEmpty()) {
        m_items = m_allFolderItems;
    } else {
        m_items.clear();
        m_items.reserve(m_allFolderItems.size());
        for (const auto& item : m_allFolderItems) {
            if (itemMatchesFilter(item, m_filterQuery, m_filterKind)) {
                m_items.push_back(item);
            }
        }
    }
    sortInternal();

    m_longestFileName.clear();
    for (const auto& item : m_items) {
        if (item.fileName().size() > m_longestFileName.size()) {
            m_longestFileName = item.fileName();
        }
    }

    scheduleFolderSizeCalculations();
}

void FileSystemModel::scheduleFolderSizeCalculations()
{
    if (!m_calculateFolderSizes || m_items.isEmpty() || m_isSearchResultsView) {
        return;
    }

    for (int i = 0; i < m_items.size(); ++i) {
        if (!m_items[i].isDir()) continue;
        const QString dirPath = m_items[i].path();
        if (m_folderSizeCache.contains(dirPath)) {
            const auto& p = m_folderSizeCache.value(dirPath);
            m_items[i].setCalculatedFolderSize(p.first, p.second);
            emit dataChanged(index(i, ColSize), index(i, ColSize));
            continue;
        }

        quint64 gen = m_generation;
        auto* watcher = new QFutureWatcher<QPair<qint64, int>>(this);
        connect(watcher, &QFutureWatcher<QPair<qint64, int>>::finished, this, [this, watcher, dirPath, gen]() {
            QPair<qint64, int> res = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation) return;
            m_folderSizeCache[dirPath] = res;
            for (int r = 0; r < m_items.size(); ++r) {
                if (m_items[r].path() == dirPath) {
                    m_items[r].setCalculatedFolderSize(res.first, res.second);
                    emit dataChanged(index(r, ColSize), index(r, ColSize));
                    break;
                }
            }
        });

        auto cancel = m_cancelFlag;
        watcher->setFuture(QtConcurrent::run([dirPath, cancel]() -> QPair<qint64, int> {
            qint64 total = 0;
            int count = 0;
            QDirIterator it(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (cancel && cancel->load(std::memory_order_relaxed)) break;
                it.next();
                QFileInfo fi = it.fileInfo();
                if (!fi.isDir()) {
                    total += fi.size();
                }
                count++;
                if (count > 50000) break;
            }
            return {total, count};
        }));
    }
}

void FileSystemModel::setFilterQuery(const QString& query, const QString& kind)
{
    beginResetModel();
    m_filterQuery = query;
    m_filterKind = kind;
    m_isSearchResultsView = false;
    applyFilterInternal();
    endResetModel();
    emit directoryLoaded(m_rootPath, static_cast<int>(m_items.size()));
}

void FileSystemModel::setSearchResults(const QVector<FileItem>& items)
{
    cancelPendingLoad();
    beginResetModel();
    m_isSearchResultsView = true;
    m_items = items;
    sortInternal();
    endResetModel();
    emit directoryLoaded(QStringLiteral("search://"), static_cast<int>(m_items.size()));
}

void FileSystemModel::appendSearchResults(const QVector<FileItem>& items)
{
    if (items.isEmpty()) return;
    int firstRow = static_cast<int>(m_items.size());
    int lastRow = firstRow + static_cast<int>(items.size()) - 1;
    beginInsertRows(QModelIndex(), firstRow, lastRow);
    m_items.append(items);
    endInsertRows();
}

void FileSystemModel::clearSearchResults()
{
    cancelPendingLoad();
    beginResetModel();
    m_isSearchResultsView = true;
    m_items.clear();
    endResetModel();
    emit directoryLoaded(QStringLiteral("search://"), 0);
}

void FileSystemModel::exitSearchResults()
{
    if (!m_isSearchResultsView) return;
    cancelPendingLoad();
    beginResetModel();
    m_isSearchResultsView = false;
    m_filterQuery.clear();
    m_filterKind.clear();
    m_items = m_allFolderItems;
    sortInternal();
    endResetModel();
    emit directoryLoaded(m_rootPath, static_cast<int>(m_items.size()));
}

void FileSystemModel::sortInternal()
{
    if (m_items.size() < 2) return;

    struct SortEntry {
        int index{0};
        bool isDir{false};
        QString text;
        qint64 number{0};
        QDateTime date;
    };

    // Fold strings once instead of on every comparison.
    QVector<SortEntry> entries;
    entries.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        const FileItem& item = m_items.at(i);
        SortEntry entry;
        entry.index = i;
        entry.isDir = item.isDir();
        switch (m_sortColumn) {
        case ColDateModified:
            entry.date = item.modified();
            break;
        case ColSize:
            entry.number = item.size();
            break;
        case ColKind:
            entry.text = item.kind().toCaseFolded();
            break;
        case ColName:
        default:
            entry.text = item.fileName().toCaseFolded();
            break;
        }
        entries.append(std::move(entry));
    }

    const bool ascending = (m_sortOrder == Qt::AscendingOrder);
    const bool dirsFirst = m_directoriesFirst;
    const int sortColumn = m_sortColumn;

    std::stable_sort(entries.begin(), entries.end(), [dirsFirst, ascending, sortColumn](const SortEntry& a, const SortEntry& b) {
        // Folders stay on top regardless of ascending/descending order.
        if (dirsFirst && a.isDir != b.isDir) {
            return a.isDir;
        }

        int comparison = 0;
        switch (sortColumn) {
        case ColDateModified:
            comparison = (a.date == b.date) ? 0 : (a.date < b.date ? -1 : 1);
            break;
        case ColSize:
            comparison = (a.number == b.number) ? 0 : (a.number < b.number ? -1 : 1);
            break;
        case ColKind:
        case ColName:
        default:
            comparison = QString::compare(a.text, b.text, Qt::CaseSensitive);
            break;
        }
        return ascending ? comparison < 0 : comparison > 0;
    });

    QVector<FileItem> sorted;
    sorted.reserve(entries.size());
    for (const auto& entry : entries) {
        sorted.append(m_items.at(entry.index));
    }
    m_items = std::move(sorted);
}

void FileSystemModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= ColumnCount) return;
    m_sortColumn = column;
    m_sortOrder = order;

    // Preserve selection/current item across a sort instead of resetting the model.
    const QModelIndexList oldPersistent = persistentIndexList();
    QVector<QPair<QModelIndex, QString>> oldPaths;
    oldPaths.reserve(oldPersistent.size());
    for (const auto& idx : oldPersistent) {
        if (idx.row() >= 0 && idx.row() < m_items.size()) {
            oldPaths.append({idx, m_items.at(idx.row()).path()});
        }
    }

    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    sortInternal();

    QHash<QString, int> rowByPath;
    rowByPath.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        rowByPath.insert(m_items.at(i).path(), i);
    }

    QModelIndexList from;
    QModelIndexList to;
    from.reserve(oldPaths.size());
    to.reserve(oldPaths.size());
    for (const auto& entry : oldPaths) {
        from.append(entry.first);
        const auto row = rowByPath.constFind(entry.second);
        to.append(row != rowByPath.cend() ? index(row.value(), 0) : QModelIndex());
    }
    changePersistentIndexList(from, to);

    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    emit sortChanged(m_sortColumn, m_sortOrder);
}

QModelIndex FileSystemModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= m_items.size() || column < 0 || column >= ColumnCount) {
        return {};
    }
    return createIndex(row, column);
}

QModelIndex FileSystemModel::parent(const QModelIndex& /*child*/) const
{
    return {};
}

int FileSystemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_items.size());
}

int FileSystemModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant FileSystemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const FileItem& item = m_items.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:
            return item.fileName();
        case ColDateModified:
            return item.modified().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        case ColSize:
            return item.formattedSize();
        case ColKind:
            return item.kind();
        default:
            return {};
        }
    }

    if (role == Qt::DecorationRole && index.column() == ColName) {
        return item.icon();
    }

    if (role == Qt::ToolTipRole) {
        if (m_isSearchResultsView) {
            return QStringLiteral("%1\nPath: %2\nSize: %3\nModified: %4\nKind: %5")
                .arg(item.fileName(), item.path(), item.formattedSize(), item.modified().toString(), item.kind());
        }
        return QStringLiteral("%1\nSize: %2\nModified: %3\nKind: %4")
            .arg(item.fileName(), item.formattedSize(), item.modified().toString(), item.kind());
    }

    switch (role) {
    case FilePathRole:
        return item.path();
    case FileNameRole:
        return item.fileName();
    case FileSizeRole:
        return item.size();
    case FileFormattedSizeRole:
        return item.formattedSize();
    case FileDateRole:
        return item.modified();
    case FileKindRole:
        return item.kind();
    case FileIsDirRole:
        return item.isDir();
    case FileIsSymLinkRole:
        return item.isSymLink();
    case FilePermissionsRole:
        return item.permissionsString();
    case FileOwnerRole:
        return item.owner();
    case FileGroupRole:
        return item.group();
    case FileTagsRole:
        return item.tags();
    default:
        break;
    }

    return {};
}

QVariant FileSystemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case ColName:
            return QStringLiteral("Name");
        case ColDateModified:
            return QStringLiteral("Date Modified");
        case ColSize:
            return QStringLiteral("Size");
        case ColKind:
            return QStringLiteral("Kind");
        default:
            return {};
        }
    }
    return {};
}

Qt::ItemFlags FileSystemModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    const FileItem* item = itemForIndex(index);
    if (item && item->isDir()) {
        f |= Qt::ItemIsDropEnabled;
    }
    if (index.column() == ColName && item && !m_isSearchResultsView &&
        !item->path().isEmpty() &&
        !item->path().startsWith(QStringLiteral("trash://")) &&
        !item->path().startsWith(QStringLiteral("tag://"))) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

bool FileSystemModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    if (index.column() != ColName) {
        return false;
    }
    const FileItem* item = itemForIndex(index);
    if (!item) return false;

    QString newName = value.toString().trimmed();
    if (newName.isEmpty() || newName == item->fileName()) {
        return false;
    }
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))) {
        return false;
    }

    QString oldPath = item->path();
    FileOperationEngine engine;
    QString renamedPath, error;
    if (engine.renameItem(oldPath, newName, &renamedPath, &error)) {
        emit itemRenamed(oldPath, renamedPath);
        refresh();
        return true;
    }
    return false;
}

Qt::DropActions FileSystemModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

QStringList FileSystemModel::mimeTypes() const
{
    return {QStringLiteral("text/uri-list")};
}

QMimeData* FileSystemModel::mimeData(const QModelIndexList& indexes) const
{
    auto* mime = new QMimeData();
    QList<QUrl> urls;
    QSet<int> seenRows;

    for (const auto& idx : indexes) {
        if (!idx.isValid() || seenRows.contains(idx.row())) continue;
        seenRows.insert(idx.row());
        const FileItem* item = itemForIndex(idx);
        if (item) {
            urls.append(QUrl::fromLocalFile(item->path()));
        }
    }

    mime->setUrls(urls);
    return mime;
}

bool FileSystemModel::canDropMimeData(const QMimeData* data, Qt::DropAction /*action*/, int /*row*/, int /*column*/, const QModelIndex& parent) const
{
    if (!data || !data->hasUrls()) {
        return false;
    }
    if (parent.isValid()) {
        const FileItem* item = itemForIndex(parent);
        return item && item->isDir();
    }
    return true;
}

bool FileSystemModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int /*row*/, int /*column*/, const QModelIndex& parent)
{
    if (!data || !data->hasUrls()) {
        return false;
    }

    QString targetDir;
    if (parent.isValid()) {
        const FileItem* item = itemForIndex(parent);
        if (item && item->isDir()) {
            targetDir = item->path();
        } else {
            return false;
        }
    } else {
        targetDir = m_rootPath;
    }

    QStringList sources;
    for (const auto& url : data->urls()) {
        if (url.isLocalFile()) {
            QString localPath = url.toLocalFile();
            // Do not drop a directory into itself or its children
            if (targetDir.startsWith(localPath)) {
                continue;
            }
            sources.append(localPath);
        }
    }

    if (sources.isEmpty() || targetDir.isEmpty()) {
        return false;
    }

    emit fileDropRequested(sources, targetDir, action);
    return true;
}

const FileItem* FileSystemModel::itemAt(int row) const
{
    if (row < 0 || row >= m_items.size()) return nullptr;
    return &m_items.at(row);
}

const FileItem* FileSystemModel::itemForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) return nullptr;
    return itemAt(index.row());
}

QModelIndex FileSystemModel::indexForPath(const QString& path) const
{
    QString clean = QDir::cleanPath(path);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].path() == clean) {
            return index(i, 0);
        }
    }
    return {};
}
