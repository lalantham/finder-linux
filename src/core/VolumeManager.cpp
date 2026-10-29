#include "VolumeManager.h"
#include <QFileIconProvider>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QSet>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

bool VolumeManager::isUsbDevice(const QString& devPathOrName)
{
    QString dev = devPathOrName.trimmed();
    if (dev.isEmpty()) return false;

    // Check if path indicates MTP/PTP/GVFS
    if (dev.contains(QStringLiteral("mtp"), Qt::CaseInsensitive) ||
        dev.contains(QStringLiteral("ptp"), Qt::CaseInsensitive) ||
        dev.contains(QStringLiteral("/gvfs/"))) {
        return true;
    }

    // Extract device node name, e.g. "sdd1" from "/dev/sdd1"
    QString node = QFileInfo(dev).fileName();
    if (node.isEmpty()) return false;

    // Check sysfs link: /sys/class/block/<node>
    QFileInfo sysBlock(QStringLiteral("/sys/class/block/") + node);
    if (sysBlock.exists()) {
        QString canon = sysBlock.canonicalFilePath();
        if (canon.contains(QStringLiteral("/usb"))) {
            return true;
        }
    }

    // Strip trailing digits to check parent disk (e.g. sdd1 -> sdd, nvme0n1p1 -> nvme0n1)
    QString parentDisk = node;
    while (!parentDisk.isEmpty() && parentDisk.back().isDigit()) {
        parentDisk.chop(1);
    }
    // Handle nvme partitions ending with 'p' (e.g. nvme0n1p -> nvme0n1)
    if (parentDisk.endsWith(QLatin1Char('p')) && parentDisk.contains(QStringLiteral("nvme"))) {
        parentDisk.chop(1);
    }
    if (!parentDisk.isEmpty() && parentDisk != node) {
        QFileInfo parentBlock(QStringLiteral("/sys/class/block/") + parentDisk);
        if (parentBlock.exists() && parentBlock.canonicalFilePath().contains(QStringLiteral("/usb"))) {
            return true;
        }
    }

    return false;
}

QVector<VolumeEntry> VolumeManager::mountedVolumes()
{
    QVector<VolumeEntry> list;
    QList<QStorageInfo> storageList = QStorageInfo::mountedVolumes();
    QFileIconProvider iconProvider;
    QSet<QString> seenPaths;
    QSet<QString> seenDevices;

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
            fsType == QStringLiteral("tracefs") ||
            fsType == QStringLiteral("configfs") ||
            fsType == QStringLiteral("debugfs") ||
            fsType == QStringLiteral("fusectl") ||
            fsType == QStringLiteral("mqueue")) {
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
            (path.startsWith(QStringLiteral("/run")) && !path.startsWith(QStringLiteral("/run/media")) && !path.contains(QStringLiteral("/gvfs"))) ||
            path == QStringLiteral("/root") ||
            path == QStringLiteral("/srv") ||
            path == QStringLiteral("/home")) {
            continue;
        }

        // Avoid duplicate paths
        if (seenPaths.contains(path)) continue;
        seenPaths.insert(path);

        QString devPath = QString::fromUtf8(storage.device());
        if (!devPath.isEmpty()) {
            seenDevices.insert(devPath);
            seenDevices.insert(QFileInfo(devPath).fileName());
        }

        VolumeEntry entry;
        entry.path = path;
        entry.mountPoint = path;
        entry.device = devPath;
        entry.fsType = fsType;
        entry.totalBytes = storage.bytesTotal();
        entry.availableBytes = storage.bytesAvailable();
        entry.isRoot = (path == QStringLiteral("/"));
        entry.isMounted = true;
        entry.isUsb = isUsbDevice(devPath) || isUsbDevice(path);

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

            if (entry.isUsb) {
                entry.isRemovable = true;
                entry.icon = QIcon::fromTheme(QStringLiteral("drive-removable-media-usb"),
                             QIcon::fromTheme(QStringLiteral("drive-removable-media"),
                             QIcon::fromTheme(QStringLiteral("media-removable"),
                             iconProvider.icon(QFileIconProvider::Drive))));
            } else if (path.startsWith(QStringLiteral("/media")) ||
                       path.startsWith(QStringLiteral("/run/media")) ||
                       path.startsWith(QStringLiteral("/mnt")) ||
                       storage.isReadOnly()) {
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

    // Discover unmounted USB/removable block devices via lsblk
    QProcess lsblkProc;
    lsblkProc.start(QStringLiteral("lsblk"), {
        QStringLiteral("-J"),
        QStringLiteral("-b"),
        QStringLiteral("-o"),
        QStringLiteral("NAME,PATH,LABEL,MOUNTPOINT,FSTYPE,SIZE,RM,HOTPLUG,TRAN,MODEL")
    });
    if (lsblkProc.waitForFinished(1000) && lsblkProc.exitCode() == 0) {
        QJsonDocument doc = QJsonDocument::fromJson(lsblkProc.readAllStandardOutput());
        QJsonObject root = doc.object();
        QJsonArray blockdevices = root.value(QStringLiteral("blockdevices")).toArray();

        auto checkAndAddDevice = [&](const QJsonObject& devObj, const QString& parentTran, const QString& parentModel, bool parentRm, bool parentHotplug) {
            QString name = devObj.value(QStringLiteral("name")).toString();
            QString path = devObj.value(QStringLiteral("path")).toString();
            QString label = devObj.value(QStringLiteral("label")).toString();
            QString mp = devObj.value(QStringLiteral("mountpoint")).toString();
            QString fstype = devObj.value(QStringLiteral("fstype")).toString();
            qint64 size = static_cast<qint64>(devObj.value(QStringLiteral("size")).toDouble());
            QString tran = devObj.value(QStringLiteral("tran")).toString();
            if (tran.isEmpty()) tran = parentTran;
            bool rm = devObj.value(QStringLiteral("rm")).toBool() || parentRm;
            bool hotplug = devObj.value(QStringLiteral("hotplug")).toBool() || parentHotplug;

            // Skip swap, zram, or tiny partitions
            if (fstype.compare(QStringLiteral("swap"), Qt::CaseInsensitive) == 0 || name.startsWith(QStringLiteral("zram"))) {
                return;
            }
            if (size > 0 && size < 1048576) {
                return; // Less than 1MB
            }

            bool isUsb = (tran.compare(QStringLiteral("usb"), Qt::CaseInsensitive) == 0) || isUsbDevice(path) || isUsbDevice(name);
            bool isRem = isUsb || rm || hotplug;

            // If it's already mounted, update isUsb on existing entry if appropriate
            if (!mp.isEmpty() || seenPaths.contains(mp) || seenDevices.contains(path) || seenDevices.contains(name)) {
                if (isUsb) {
                    for (auto& entry : list) {
                        if (entry.path == mp || entry.device == path) {
                            entry.isUsb = true;
                            entry.isRemovable = true;
                            entry.icon = QIcon::fromTheme(QStringLiteral("drive-removable-media-usb"),
                                         QIcon::fromTheme(QStringLiteral("drive-removable-media"),
                                         iconProvider.icon(QFileIconProvider::Drive)));
                        }
                    }
                }
                return;
            }

            // Only add unmounted devices if they are USB or removable
            if (!isRem) {
                return;
            }

            VolumeEntry unmountedEntry;
            unmountedEntry.device = path;
            unmountedEntry.path = path;
            unmountedEntry.mountPoint = QString();
            unmountedEntry.fsType = fstype;
            unmountedEntry.totalBytes = size;
            unmountedEntry.availableBytes = size;
            unmountedEntry.isRemovable = true;
            unmountedEntry.isUsb = isUsb;
            unmountedEntry.isMounted = false;
            unmountedEntry.isRoot = false;

            QString displayName = label;
            if (displayName.isEmpty()) {
                QString model = devObj.value(QStringLiteral("model")).toString();
                if (model.isEmpty()) model = parentModel;
                if (!model.isEmpty()) {
                    displayName = model;
                } else {
                    displayName = isUsb ? QStringLiteral("USB Drive (%1)").arg(name) : name;
                }
            }
            unmountedEntry.name = displayName;
            unmountedEntry.icon = isUsb
                ? QIcon::fromTheme(QStringLiteral("drive-removable-media-usb"),
                     QIcon::fromTheme(QStringLiteral("drive-removable-media"),
                     QIcon::fromTheme(QStringLiteral("media-removable"),
                     iconProvider.icon(QFileIconProvider::Drive))))
                : QIcon::fromTheme(QStringLiteral("drive-removable-media"),
                     QIcon::fromTheme(QStringLiteral("media-removable"),
                     iconProvider.icon(QFileIconProvider::Drive)));

            seenDevices.insert(path);
            list.append(unmountedEntry);
        };

        for (const auto& devVal : blockdevices) {
            QJsonObject devObj = devVal.toObject();
            QString pTran = devObj.value(QStringLiteral("tran")).toString();
            QString pModel = devObj.value(QStringLiteral("model")).toString();
            bool pRm = devObj.value(QStringLiteral("rm")).toBool();
            bool pHotplug = devObj.value(QStringLiteral("hotplug")).toBool();

            QJsonArray children = devObj.value(QStringLiteral("children")).toArray();
            if (children.isEmpty()) {
                checkAndAddDevice(devObj, pTran, pModel, pRm, pHotplug);
            } else {
                for (const auto& childVal : children) {
                    checkAndAddDevice(childVal.toObject(), pTran, pModel, pRm, pHotplug);
                }
            }
        }
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
    entry.isMounted = true;
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

QString VolumeManager::mountDevice(const QString& device, QString* errorOut)
{
    QString dev = device.trimmed();
    if (dev.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Device path is empty.");
        return QString();
    }

    // 1. Try udisksctl mount -b <dev> --no-user-interaction
    QProcess udisksProc;
    udisksProc.start(QStringLiteral("udisksctl"), {
        QStringLiteral("mount"),
        QStringLiteral("-b"),
        dev,
        QStringLiteral("--no-user-interaction")
    });
    if (udisksProc.waitForFinished(6000) && udisksProc.exitCode() == 0) {
        QString out = QString::fromUtf8(udisksProc.readAllStandardOutput()).trimmed();
        // Standard output: "Mounted /dev/sdX1 at /run/media/... ."
        static const QRegularExpression rx(QStringLiteral("at\\s+(.+?)\\.?$"));
        auto match = rx.match(out);
        if (match.hasMatch()) {
            QString mp = match.captured(1).trimmed();
            if (mp.endsWith(QLatin1Char('.'))) mp.chop(1);
            if (!mp.isEmpty() && QDir(mp).exists()) {
                return mp;
            }
        }
    }

    // 2. Try gio mount -d <dev>
    QProcess gioProc;
    gioProc.start(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("-d"), dev});
    if (gioProc.waitForFinished(6000) && gioProc.exitCode() == 0) {
        QProcess findmntProc;
        findmntProc.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("TARGET"), dev});
        if (findmntProc.waitForFinished(2000) && findmntProc.exitCode() == 0) {
            QString mp = QString::fromUtf8(findmntProc.readAllStandardOutput()).trimmed();
            if (!mp.isEmpty() && QDir(mp).exists()) return mp;
        }
    }

    // 3. Fallback: check findmnt
    QProcess findmntProc;
    findmntProc.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("TARGET"), dev});
    if (findmntProc.waitForFinished(2000) && findmntProc.exitCode() == 0) {
        QString mp = QString::fromUtf8(findmntProc.readAllStandardOutput()).trimmed();
        if (!mp.isEmpty() && QDir(mp).exists()) return mp;
    }

    if (errorOut) {
        QString err = QString::fromUtf8(udisksProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QString::fromUtf8(gioProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QStringLiteral("Failed to mount %1").arg(dev);
        *errorOut = err;
    }
    return QString();
}

bool VolumeManager::unmountVolume(const QString& target, QString* errorOut)
{
    QString clean = QDir::cleanPath(target);
    if (clean == QStringLiteral("/") || clean.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Cannot unmount the root filesystem.");
        return false;
    }

    QString dev;
    if (clean.startsWith(QStringLiteral("/dev/"))) {
        dev = clean;
    } else {
        // 1. Try udisksctl unmount with device from findmnt
        QProcess findmntProc;
        findmntProc.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("SOURCE"), clean});
        if (findmntProc.waitForFinished(1000) && findmntProc.exitCode() == 0) {
            dev = QString::fromUtf8(findmntProc.readAllStandardOutput()).trimmed();
            if (!dev.isEmpty()) {
                qsizetype bracket = dev.indexOf(QLatin1Char('['));
                if (bracket != -1) {
                    dev = dev.left(bracket).trimmed();
                }
            }
        }
    }

    if (!dev.isEmpty() && dev.startsWith(QStringLiteral("/dev/"))) {
        QProcess udisksProc;
        udisksProc.start(QStringLiteral("udisksctl"), {QStringLiteral("unmount"), QStringLiteral("-b"), dev, QStringLiteral("--no-user-interaction")});
        if (udisksProc.waitForFinished(4000) && udisksProc.exitCode() == 0) {
            return true;
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
