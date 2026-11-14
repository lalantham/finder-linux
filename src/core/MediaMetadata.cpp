#include "MediaMetadata.h"
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString formatBitrate(qint64 bitsPerSecond) {
    if (bitsPerSecond <= 0)
        return {};
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1000);
}

QString formatDuration(double seconds) {
    if (seconds <= 0.0)
        return {};
    const qint64 total = static_cast<qint64>(seconds);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 secs = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

} // namespace

MediaMetadata MediaMetadataReader::inspect(const QString& path) {
    MediaMetadata metadata;
    const QFileInfo info(path);
    if (!info.exists() || info.isDir())
        return metadata;

    QImageReader reader(path);
    const QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        metadata.dimensions =
            QStringLiteral("%1 × %2 px").arg(imageSize.width()).arg(imageSize.height());
    }

    const QString identify = QStandardPaths::findExecutable(QStringLiteral("identify"));
    if (!identify.isEmpty()) {
        QProcess process;
        process.start(identify, {QStringLiteral("-format"),
                                 QStringLiteral("Camera make: %[EXIF:Make]\n"
                                                "Camera model: %[EXIF:Model]\n"
                                                "Captured: %[EXIF:DateTimeOriginal]\n"
                                                "Aperture: f/%[EXIF:FNumber]\n"
                                                "Exposure: %[EXIF:ExposureTime]\n"
                                                "ISO: %[EXIF:ISOSpeedRatings]\n"
                                                "Focal length: %[EXIF:FocalLength]"),
                                 info.absoluteFilePath()});
        if (process.waitForFinished(2500) && process.exitCode() == 0) {
            const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput())
                                          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                const QString trimmed = line.trimmed();
                if (trimmed.isEmpty() || trimmed.endsWith(QStringLiteral(": ")))
                    continue;
                metadata.exif.append(trimmed);
            }
        }
    }

    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!ffprobe.isEmpty()) {
        QProcess process;
        process.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                                QStringLiteral("-print_format"), QStringLiteral("json"),
                                QStringLiteral("-show_streams"), QStringLiteral("-show_format"),
                                info.absoluteFilePath()});
        if (process.waitForFinished(3500) && process.exitCode() == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
            const QJsonObject root = doc.object();
            const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
            for (const QJsonValue& value : streams) {
                const QJsonObject stream = value.toObject();
                if (metadata.codec.isEmpty()) {
                    metadata.codec = stream.value(QStringLiteral("codec_name")).toString();
                }
                if (metadata.dimensions.isEmpty()) {
                    const int width = stream.value(QStringLiteral("width")).toInt();
                    const int height = stream.value(QStringLiteral("height")).toInt();
                    if (width > 0 && height > 0) {
                        metadata.dimensions = QStringLiteral("%1 × %2 px").arg(width).arg(height);
                    }
                }
                if (metadata.duration.isEmpty()) {
                    metadata.duration = formatDuration(
                        stream.value(QStringLiteral("duration")).toString().toDouble());
                }
                if (metadata.bitrate.isEmpty()) {
                    metadata.bitrate = formatBitrate(
                        stream.value(QStringLiteral("bit_rate")).toString().toLongLong());
                }
                if (!metadata.codec.isEmpty() && !metadata.dimensions.isEmpty() &&
                    !metadata.duration.isEmpty()) {
                    break;
                }
            }
            const QJsonObject format = root.value(QStringLiteral("format")).toObject();
            if (metadata.duration.isEmpty()) {
                metadata.duration =
                    formatDuration(format.value(QStringLiteral("duration")).toString().toDouble());
            }
            if (metadata.bitrate.isEmpty()) {
                metadata.bitrate =
                    formatBitrate(format.value(QStringLiteral("bit_rate")).toString().toLongLong());
            }
        }
    }

    return metadata;
}
