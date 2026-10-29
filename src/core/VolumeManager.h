#pragma once

#include <QString>
#include <QVector>
#include <QStorageInfo>
#include <QIcon>

struct VolumeEntry {
    QString name;
    QString path;            // Mount point if mounted, or block device (/dev/...) if unmounted
    QString mountPoint;      // Mount point or empty if unmounted
    QString device;          // Device node path e.g. /dev/sdd1
    QString fsType;
    qint64 totalBytes{0};
    qint64 availableBytes{0};
    bool isRemovable{false};
    bool isUsb{false};
    bool isMounted{true};
    bool isRoot{false};
    QIcon icon;
};

class VolumeManager {
public:
    static QVector<VolumeEntry> mountedVolumes();
    static VolumeEntry rootVolume();
    static bool isLocationMounted(const QString& path);
    static bool isMountPoint(const QString& path);
    static bool isUsbDevice(const QString& devPathOrName);
    static QString mountDevice(const QString& device, QString* errorOut = nullptr);
    static bool unmountVolume(const QString& mountPointOrDevice, QString* errorOut = nullptr);
};
