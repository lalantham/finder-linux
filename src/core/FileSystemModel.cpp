#include "FileSystemModel.h"
#include "TagManager.h"
#include <QFileInfo>
#include <algorithm>

FileSystemModel::FileSystemModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileSystemModel::onDirectoryChanged);
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

void FileSystemModel::setRootPath(const QString& path)
{
    m_activeTag.clear();
    QString cleanPath = QDir::cleanPath(path);
    if (m_rootPath == cleanPath) return;

    if (!m_rootPath.isEmpty()) {
        m_watcher.removePath(m_rootPath);
    }

    m_rootPath = cleanPath;
    if (!m_rootPath.isEmpty() && QDir(m_rootPath).exists()) {
        m_watcher.addPath(m_rootPath);
    }

    populate();
    emit rootPathChanged(m_rootPath);
}

void FileSystemModel::setTaggedFiles(const QString& tag)
{
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

void FileSystemModel::refresh()
{
    populate();
}

void FileSystemModel::onDirectoryChanged(const QString& path)
{
    if (path == m_rootPath) {
        populate();
    }
}

void FileSystemModel::populate()
{
    beginResetModel();
    m_items.clear();

    if (!m_activeTag.isEmpty()) {
        QStringList paths = TagManager::instance().filesForTag(m_activeTag);
        m_items.reserve(paths.size());
        for (const auto& p : paths) {
            if (QFileInfo::exists(p)) {
                m_items.emplace_back(p);
            }
        }
        sortInternal();
    } else if (!m_rootPath.isEmpty()) {
        QDir dir(m_rootPath);
        QDir::Filters filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System;
        if (m_showHidden) {
            filters |= QDir::Hidden;
        }

        QFileInfoList entries = dir.entryInfoList(filters, QDir::NoSort);
        m_items.reserve(entries.size());
        for (const auto& fi : entries) {
            m_items.emplace_back(fi);
        }

        sortInternal();
    }

    endResetModel();
    emit directoryLoaded(m_rootPath, static_cast<int>(m_items.size()));
}

void FileSystemModel::sortInternal()
{
    auto comparator = [this](const FileItem& a, const FileItem& b) -> bool {
        if (m_directoriesFirst) {
            if (a.isDir() != b.isDir()) {
                return a.isDir(); // Directories always first
            }
        }

        bool less = false;
        switch (m_sortColumn) {
        case ColName:
            less = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive) < 0;
            break;
        case ColDateModified:
            less = a.modified() < b.modified();
            break;
        case ColSize:
            less = a.size() < b.size();
            break;
        case ColKind:
            less = QString::compare(a.kind(), b.kind(), Qt::CaseInsensitive) < 0;
            break;
        default:
            less = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive) < 0;
            break;
        }

        return (m_sortOrder == Qt::AscendingOrder) ? less : !less;
    };

    std::sort(m_items.begin(), m_items.end(), comparator);
}

void FileSystemModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= ColumnCount) return;
    m_sortColumn = column;
    m_sortOrder = order;

    beginResetModel();
    sortInternal();
    endResetModel();
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
    return f;
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
