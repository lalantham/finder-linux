#include "VolumeManager.h"
#include <QFileIconProvider>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QSet>
#include <QStorageInfo>
#include <QUuid>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {
QVector<VolumeEntry> s_cachedVolumes;
QElapsedTimer s_cacheTimestamp;
constexpr qint64 CacheLifetimeMs = 2000;

QString deviceForTarget(const QString& target)
{
    if (target.startsWith(QStringLiteral("/dev/"))) {
        return target;
    }

    // QStorageInfo avoids spawning findmnt for the common case.
    const QStorageInfo storage(target);
    if (storage.isValid() && storage.isReady()) {
        const QString dev = QString::fromUtf8(storage.device()).trimmed();
        if (dev.startsWith(QStringLiteral("/dev/"))) {
            return dev;
        }
    }

    QProcess findmntProc;
    findmntProc.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("SOURCE"), target});
    if (!findmntProc.waitForFinished(1000) || findmntProc.exitCode() != 0) {
        return {};
    }

    QString dev = QString::fromUtf8(findmntProc.readAllStandardOutput()).trimmed();
    const qsizetype bracket = dev.indexOf(QLatin1Char('['));
    if (bracket != -1) {
        dev = dev.left(bracket).trimmed();
    }
    return dev.startsWith(QStringLiteral("/dev/")) ? dev : QString();
}

QString udisksObjectPathForDevice(const QString& device)
{
    if (!device.startsWith(QStringLiteral("/dev/"))) {
        return {};
    }

    QString resolved = QFileInfo(device).canonicalFilePath();
    if (resolved.isEmpty()) {
        resolved = device;
    }
    const QString name = QFileInfo(resolved).fileName();
    if (name.isEmpty()) {
        return {};
    }
    return QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + name;
}

QDBusMessage callUdisks(const QString& objectPath, const QString& interface,
                        const QString& method, const QVariantList& arguments,
                        int timeoutMs)
{
    // Use a private connection: VolumeManager methods run on QtConcurrent
    // worker threads while the main thread owns the shared system-bus
    // connection for UDisks2 signals.
    const QString connectionName = QStringLiteral("finder-udisks-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SystemBus, connectionName);
    if (!bus.isConnected()) {
        return QDBusMessage::createError(
            QDBusError::Disconnected, QStringLiteral("System D-Bus is unavailable"));
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"), objectPath, interface, method);
    message.setArguments(arguments);
    const QDBusMessage reply = bus.call(message, QDBus::Block, timeoutMs);
    QDBusConnection::disconnectFromBus(connectionName);
    return reply;
}

bool udisksMount(const QString& device, QString* mountPoint, QString* errorOut)
{
    const QString objectPath = udisksObjectPathForDevice(device);
    if (objectPath.isEmpty()) {
        return false;
    }

    const QDBusMessage reply = callUdisks(
        objectPath, QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
        QStringLiteral("Mount"), {QVariantMap()}, 15000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (errorOut) *errorOut = reply.errorMessage();
        return false;
    }

    const QString result = reply.arguments().value(0).toString();
    if (result.isEmpty() || !QDir(result).exists()) {
        if (errorOut) *errorOut = QStringLiteral("UDisks2 did not return a valid mount point.");
        return false;
    }

    if (mountPoint) *mountPoint = result;
    return true;
}

bool udisksUnmount(const QString& device, QString* errorOut)
{
    const QString objectPath = udisksObjectPathForDevice(device);
    if (objectPath.isEmpty()) {
        return false;
    }

    const QDBusMessage reply = callUdisks(
        objectPath, QStringLiteral("org.freedesktop.UDisks2.Filesystem"),
        QStringLiteral("Unmount"), {QVariantMap()}, 10000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (errorOut) *errorOut = reply.errorMessage();
        return false;
    }
    return true;
}

QString udisksDrivePath(const QString& device)
{
    const QString objectPath = udisksObjectPathForDevice(device);
    if (objectPath.isEmpty()) {
        return {};
    }

    const QDBusMessage reply = callUdisks(
        objectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"),
        {QStringLiteral("org.freedesktop.UDisks2.Block"), QStringLiteral("Drive")},
        5000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        return {};
    }

    QVariant value = reply.arguments().first();
    if (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    if (value.canConvert<QDBusObjectPath>()) {
        return value.value<QDBusObjectPath>().path();
    }
    return value.toString();
}

bool udisksDriveCall(const QString& drivePath, const QString& method, QString* errorOut)
{
    if (drivePath.isEmpty() || drivePath == QStringLiteral("/")) {
        return false;
    }

    const QDBusMessage reply = callUdisks(
        drivePath, QStringLiteral("org.freedesktop.UDisks2.Drive"), method,
        {QVariantMap()}, 15000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (errorOut) *errorOut = reply.errorMessage();
        return false;
    }
    return true;
}

bool udisksEject(const QString& device, QString* errorOut)
{
    const QString drivePath = udisksDrivePath(device);
    if (drivePath.isEmpty() || drivePath == QStringLiteral("/")) {
        return false;
    }

    QString ejectError;
    if (udisksDriveCall(drivePath, QStringLiteral("Eject"), &ejectError)) {
        return true;
    }

    QString powerOffError;
    if (udisksDriveCall(drivePath, QStringLiteral("PowerOff"), &powerOffError)) {
        return true;
    }

    if (errorOut) {
        *errorOut = powerOffError.isEmpty() ? ejectError : powerOffError;
    }
    return false;
}
}

void VolumeManager::invalidateCache()
{
    s_cachedVolumes.clear();
    s_cacheTimestamp.invalidate();
}

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
    // lsblk spawns a process and probing every mount is expensive; the sidebar
    // asks for this several times per refresh, so collapse those calls.
    if (s_cacheTimestamp.isValid() && s_cacheTimestamp.elapsed() < CacheLifetimeMs) {
        return s_cachedVolumes;
    }

    QVector<VolumeEntry> list;
    QList<QStorageInfo> storageList = QStorageInfo::mountedVolumes();
    QFileIconProvider iconProvider;
    QSet<QString> seenPaths;
    QSet<QString> seenDevices;

    for (const auto& storage : storageList) {
        if (!storage.isValid() || !storage.isReady()) continue;
        QString path = storage.rootPath();

        // Skip pseudo filesystems (sysfs, proc, devtmpfs, cgroup, overlay, etc.).
        // The root volume is always kept: containers and live systems commonly
        // expose it as overlay/squashfs, and it must still appear in the sidebar.
        QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();
        const bool isRootPath = (path == QStringLiteral("/"));
        if (!isRootPath && (fsType == QStringLiteral("tmpfs") ||
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
            fsType == QStringLiteral("mqueue"))) {
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
            (path.startsWith(QStringLiteral("/run")) && !path.startsWith(QStringLiteral("/run/media")) && !path.contains(QStringLiteral("/gvfs")) && !path.contains(QStringLiteral("/kio-fuse"))) ||
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

    s_cachedVolumes = list;
    s_cacheTimestamp.start();
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
    invalidateCache();
    QString dev = device.trimmed();
    if (dev.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Device path is empty.");
        return QString();
    }

    // 1. Prefer the UDisks2 D-Bus API. This gives a structured reply and does
    //    not depend on parsing localized udisksctl output.
    QString udisksError;
    QString mountPoint;
    if (udisksMount(dev, &mountPoint, &udisksError)) {
        invalidateCache();
        return mountPoint;
    }

    // 2. Try udisksctl mount -b <dev>. Without --no-user-interaction, polkit
    //    can prompt through the desktop's authentication agent when needed.
    QProcess udisksProc;
    udisksProc.start(QStringLiteral("udisksctl"), {
        QStringLiteral("mount"),
        QStringLiteral("-b"),
        dev
    });
    if (udisksProc.waitForFinished(15000) && udisksProc.exitCode() == 0) {
        QString out = QString::fromUtf8(udisksProc.readAllStandardOutput()).trimmed();
        // Standard output: "Mounted /dev/sdX1 at /run/media/... ."
        static const QRegularExpression rx(QStringLiteral("at\\s+(.+?)\\.?$"));
        auto match = rx.match(out);
        if (match.hasMatch()) {
            QString mp = match.captured(1).trimmed();
            if (mp.endsWith(QLatin1Char('.'))) mp.chop(1);
            if (!mp.isEmpty() && QDir(mp).exists()) {
                invalidateCache();
                return mp;
            }
        }
    }

    // 2. Try gio mount -d <dev>
    QProcess gioProc;
    gioProc.start(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("-d"), dev});
    if (gioProc.waitForFinished(15000) && gioProc.exitCode() == 0) {
        QProcess findmntProc;
        findmntProc.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("TARGET"), dev});
        if (findmntProc.waitForFinished(2000) && findmntProc.exitCode() == 0) {
            QString mp = QString::fromUtf8(findmntProc.readAllStandardOutput()).trimmed();
            if (!mp.isEmpty() && QDir(mp).exists()) {
                invalidateCache();
                return mp;
            }
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
        QString err = udisksError;
        if (err.isEmpty()) err = QString::fromUtf8(udisksProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QString::fromUtf8(gioProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QStringLiteral("Failed to mount %1").arg(dev);
        *errorOut = err;
    }
    return QString();
}

bool VolumeManager::unmountVolume(const QString& target, QString* errorOut)
{
    invalidateCache();
    QString clean = QDir::cleanPath(target);
    if (clean == QStringLiteral("/") || clean.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Cannot unmount the root filesystem.");
        return false;
    }

    QString dev;
    if (clean.startsWith(QStringLiteral("/dev/"))) {
        dev = clean;
    } else {
        dev = deviceForTarget(clean);
    }

    QString udisksError;
    if (!dev.isEmpty() && dev.startsWith(QStringLiteral("/dev/"))) {
        if (udisksUnmount(dev, &udisksError)) {
            invalidateCache();
            return true;
        }

        QProcess udisksProc;
        udisksProc.start(QStringLiteral("udisksctl"), {QStringLiteral("unmount"), QStringLiteral("-b"), dev});
        if (udisksProc.waitForFinished(10000) && udisksProc.exitCode() == 0) {
            invalidateCache();
            return true;
        }
    }

    // 2. Try gio mount -u <clean>
    QProcess gioProc;
    gioProc.start(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("-u"), clean});
    if (gioProc.waitForFinished(8000) && gioProc.exitCode() == 0) {
        invalidateCache();
        return true;
    }

    // 3. Fallback: umount
    QProcess umountProc;
    umountProc.start(QStringLiteral("umount"), {clean});
    if (umountProc.waitForFinished(3000) && umountProc.exitCode() == 0) {
        invalidateCache();
        return true;
    }

    if (errorOut) {
        QString err = udisksError;
        if (err.isEmpty()) err = QString::fromUtf8(gioProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QString::fromUtf8(umountProc.readAllStandardError()).trimmed();
        if (err.isEmpty()) err = QStringLiteral("Failed to unmount device. It might be busy or require privileges.");
        *errorOut = err;
    }
    return false;
}

bool VolumeManager::ejectVolume(const QString& target, QString* errorOut)
{
    // Resolve the device before unmounting; afterwards findmnt has no entry.
    const QString clean = QDir::cleanPath(target);
    const QString dev = deviceForTarget(clean);

    bool unmounted = false;
    QString udisksError;
    if (!dev.isEmpty() && dev.startsWith(QStringLiteral("/dev/"))) {
        unmounted = udisksUnmount(dev, &udisksError);
    }
    if (!unmounted) {
        if (!unmountVolume(target, errorOut)) {
            return false;
        }
    }

    // Try UDisks2 eject, then power-off for USB sticks and other drives.
    if (!dev.isEmpty() && dev.startsWith(QStringLiteral("/dev/"))) {
        QString ejectError;
        if (udisksEject(dev, &ejectError)) {
            invalidateCache();
            return true;
        }

        // Fallback: power off removable media via udisksctl.
        QProcess powerProc;
        powerProc.start(QStringLiteral("udisksctl"),
                        {QStringLiteral("power-off"), QStringLiteral("-b"), dev});
        if (powerProc.waitForFinished(4000) && powerProc.exitCode() != 0 && errorOut) {
            QString err = QString::fromUtf8(powerProc.readAllStandardError()).trimmed();
            if (err.isEmpty()) err = ejectError;
            if (err.isEmpty()) err = QStringLiteral("Failed to power off device.");
            *errorOut = err;
        }
    }

    invalidateCache();
    return true;
}
