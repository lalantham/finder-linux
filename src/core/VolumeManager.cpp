#include "VolumeManager.h"
#include <QFileIconProvider>
#include <QFileInfo>
#include <QSet>

QVector<VolumeEntry> VolumeManager::mountedVolumes()
{
    QVector<VolumeEntry> list;
    QList<QStorageInfo> storageList = QStorageInfo::mountedVolumes();
    QFileIconProvider iconProvider;
    QSet<QString> seenPaths;

    for (const auto& storage : storageList) {
        if (!storage.isValid() || !storage.isReady()) continue;
        QString path = storage.rootPath();

        // Skip pseudo filesystems (sysfs, proc, devtmpfs, cgroup, overlay, etc.)
        QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();
        if (fsType == QStringLiteral("tmpfs") ||
            fsType == QStringLiteral("devtmpfs") ||
            fsType == QStringLiteral("proc") ||
            fsType == QStringLiteral("sysfs") ||
            fsType == QStringLiteral("cgroup") ||
            fsType == QStringLiteral("cgroup2") ||
            fsType == QStringLiteral("securityfs") ||
            fsType == QStringLiteral("pstore") ||
            fsType == QStringLiteral("efivarfs") ||
            fsType == QStringLiteral("bpf") ||
            fsType == QStringLiteral("autofs") ||
            fsType == QStringLiteral("ramfs")) {
            continue;
        }

        // Avoid duplicate paths
        if (seenPaths.contains(path)) continue;
        seenPaths.insert(path);

        VolumeEntry entry;
        entry.path = path;
        entry.mountPoint = path;
        entry.fsType = fsType;
        entry.totalBytes = storage.bytesTotal();
        entry.availableBytes = storage.bytesAvailable();
        entry.isRoot = (path == QStringLiteral("/"));

        if (entry.isRoot) {
            entry.name = QStringLiteral("Root Drive");
            entry.icon = QIcon::fromTheme(QStringLiteral("drive-harddisk-root"),
                         QIcon::fromTheme(QStringLiteral("drive-harddisk"),
                         iconProvider.icon(QFileIconProvider::Drive)));
        } else {
            QString name = storage.name();
            if (name.isEmpty()) {
                name = storage.displayName();
            }
            if (name.isEmpty() || name == path) {
                name = QFileInfo(path).fileName();
            }
            if (name.isEmpty()) {
                name = path;
            }
            entry.name = name;

            if (path.startsWith(QStringLiteral("/media")) ||
                path.startsWith(QStringLiteral("/run/media")) ||
                path.startsWith(QStringLiteral("/mnt"))) {
                entry.isRemovable = true;
                entry.icon = QIcon::fromTheme(QStringLiteral("drive-removable-media"),
                             QIcon::fromTheme(QStringLiteral("media-removable"),
                             iconProvider.icon(QFileIconProvider::Drive)));
            } else {
                entry.icon = QIcon::fromTheme(QStringLiteral("drive-harddisk"),
                             iconProvider.icon(QFileIconProvider::Drive));
            }
        }

        list.append(entry);
    }

    return list;
}

VolumeEntry VolumeManager::rootVolume()
{
    VolumeEntry entry;
    QStorageInfo rootStorage(QStringLiteral("/"));
    QFileIconProvider iconProvider;

    entry.name = QStringLiteral("Root Drive");
    entry.path = QStringLiteral("/");
    entry.mountPoint = QStringLiteral("/");
    entry.fsType = QString::fromLatin1(rootStorage.fileSystemType());
    entry.totalBytes = rootStorage.bytesTotal();
    entry.availableBytes = rootStorage.bytesAvailable();
    entry.isRoot = true;
    entry.icon = QIcon::fromTheme(QStringLiteral("drive-harddisk-root"),
                 QIcon::fromTheme(QStringLiteral("drive-harddisk"),
                 iconProvider.icon(QFileIconProvider::Drive)));
    return entry;
}

bool VolumeManager::isLocationMounted(const QString& path)
{
    QStorageInfo storage(path);
    return storage.isValid() && storage.isReady();
}
