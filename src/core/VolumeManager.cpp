#include "VolumeManager.h"
#include <QFileIconProvider>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
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
            fsType == QStringLiteral("ramfs") ||
            fsType == QStringLiteral("overlay") ||
            fsType == QStringLiteral("squashfs") ||
            fsType == QStringLiteral("tracefs")) {
            continue;
        }

        // Skip internal system paths/subvolumes
        if (path.startsWith(QStringLiteral("/var")) ||
            path.startsWith(QStringLiteral("/boot")) ||
            path.startsWith(QStringLiteral("/etc")) ||
            path.startsWith(QStringLiteral("/dev")) ||
            path.startsWith(QStringLiteral("/proc")) ||
            path.startsWith(QStringLiteral("/sys")) ||
            path.startsWith(QStringLiteral("/tmp")) ||
            (path.startsWith(QStringLiteral("/run")) && !path.startsWith(QStringLiteral("/run/media"))) ||
            path == QStringLiteral("/root") ||
            path == QStringLiteral("/srv") ||
            path == QStringLiteral("/home")) {
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

bool VolumeManager::isMountPoint(const QString& path)
{
    QString clean = QDir::cleanPath(path);
    QStorageInfo storage(clean);
    return storage.isValid() && storage.isReady() && (storage.rootPath() == clean);
}

bool VolumeManager::unmountVolume(const QString& mountPoint, QString* errorOut)
{
    QString clean = QDir::cleanPath(mountPoint);
    if (clean == QStringLiteral("/") || clean.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Cannot unmount the root filesystem.");
        return false;
    }

    // 1. Try udisksctl unmount with device from findmnt
    QProcess findmntProc;
    findmntProc.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("SOURCE"), clean});
    if (findmntProc.waitForFinished(1000) && findmntProc.exitCode() == 0) {
        QString dev = QString::fromUtf8(findmntProc.readAllStandardOutput()).trimmed();
        if (!dev.isEmpty()) {
            qsizetype bracket = dev.indexOf(QLatin1Char('['));
            if (bracket != -1) {
                dev = dev.left(bracket).trimmed();
            }
            if (dev.startsWith(QStringLiteral("/dev/"))) {
                QProcess udisksProc;
                udisksProc.start(QStringLiteral("udisksctl"), {QStringLiteral("unmount"), QStringLiteral("-b"), dev});
                if (udisksProc.waitForFinished(4000) && udisksProc.exitCode() == 0) {
                    return true;
                }
            }
        }
    }

    // 2. Try gio mount -u <clean>
    QProcess gioProc;
    gioProc.start(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("-u"), clean});
    if (gioProc.waitForFinished(3000) && gioProc.exitCode() == 0) {
        return true;
    }

    // 3. Fallback: umount
    QProcess umountProc;
    umountProc.start(QStringLiteral("umount"), {clean});
    if (umountProc.waitForFinished(3000) && umountProc.exitCode() == 0) {
        return true;
    }

    if (errorOut) {
        QString err = QString::fromUtf8(gioProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QString::fromUtf8(umountProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QStringLiteral("Failed to unmount device. It might be busy or require privileges.");
        *errorOut = err;
    }
    return false;
}
