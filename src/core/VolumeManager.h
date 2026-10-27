#pragma once

#include <QString>
#include <QVector>
#include <QStorageInfo>
#include <QIcon>

struct VolumeEntry {
    QString name;
    QString path;
    QString mountPoint;
    QString fsType;
    qint64 totalBytes{0};
    qint64 availableBytes{0};
    bool isRemovable{false};
    bool isRoot{false};
    QIcon icon;
};

class VolumeManager {
public:
    static QVector<VolumeEntry> mountedVolumes();
    static VolumeEntry rootVolume();
    static bool isLocationMounted(const QString& path);
};
