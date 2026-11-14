#pragma once

#include <QString>
#include <QStringList>

struct MediaMetadata {
    QString dimensions;
    QString duration;
    QString codec;
    QString bitrate;
    QStringList exif;
};

class MediaMetadataReader {
public:
    static MediaMetadata inspect(const QString& path);
};
