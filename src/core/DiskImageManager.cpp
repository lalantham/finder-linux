#include "DiskImageManager.h"
#include "VolumeManager.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <memory>

namespace {

QString findFirstExecutable(const QStringList& names) {
    for (const QString& name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

QStringList candidateMountDevices(const QString& loopDevice) {
    QStringList preferred;
    QStringList fallback;
    QProcess lsblk;
    lsblk.start(QStringLiteral("lsblk"),
                {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
                 QStringLiteral("NAME,FSTYPE"), loopDevice});
    if (lsblk.waitForFinished(3000) && lsblk.exitCode() == 0) {
        const QJsonDocument doc = QJsonDocument::fromJson(lsblk.readAllStandardOutput());
        const QJsonArray devices = doc.object().value(QStringLiteral("blockdevices")).toArray();
        const std::function<void(const QJsonObject&)> collect = [&](const QJsonObject& object) {
            const QString name = object.value(QStringLiteral("name")).toString();
            const QString fsType = object.value(QStringLiteral("fstype")).toString();
            if (!name.isEmpty() && !fsType.isEmpty()) {
                const QString path = QStringLiteral("/dev/") + name;
                if (fsType.compare(QStringLiteral("iso9660"), Qt::CaseInsensitive) == 0 ||
                    fsType.compare(QStringLiteral("udf"), Qt::CaseInsensitive) == 0) {
                    preferred.append(path);
                } else {
                    fallback.append(path);
                }
            }
            const QJsonArray children = object.value(QStringLiteral("children")).toArray();
            for (const QJsonValue& child : children)
                collect(child.toObject());
        };
        for (const QJsonValue& value : devices)
            collect(value.toObject());
    }

    QStringList candidates = preferred + fallback;
    if (candidates.isEmpty())
        candidates.append(loopDevice);
    return candidates;
}

QString parseLoopDevice(const QString& output) {
    static const QRegularExpression re(QStringLiteral("(/dev/loop[0-9]+)"));
    const QRegularExpressionMatch match = re.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool detachLoop(const QString& loopDevice) {
    if (!loopDevice.startsWith(QStringLiteral("/dev/loop")))
        return false;
    QProcess process;
    process.start(QStringLiteral("udisksctl"),
                  {QStringLiteral("loop-delete"), QStringLiteral("-b"), loopDevice});
    return process.waitForFinished(8000) && process.exitCode() == 0;
}

} // namespace

bool DiskImageManager::isDiskImage(const QString& path) {
    static const QSet<QString> extensions = {QStringLiteral("iso"),      QStringLiteral("img"),
                                             QStringLiteral("raw"),      QStringLiteral("dmg"),
                                             QStringLiteral("squashfs"), QStringLiteral("udf")};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

QString DiskImageManager::fileDialogFilter() {
    return QStringLiteral("Disk Images (*.iso *.img *.raw *.dmg *.squashfs *.udf);;All Files (*)");
}

bool DiskImageManager::mountImage(const QString& imagePath, QString* mountPath, QString* loopDevice,
                                  QString* error) {
    const QFileInfo info(imagePath);
    if (!info.exists() || info.isDir()) {
        if (error)
            *error = QStringLiteral("The disk image does not exist.");
        return false;
    }

    QString sourcePath = info.absoluteFilePath();
    QTemporaryDir convertedDir(QDir::tempPath() + QStringLiteral("/file-manager-dmg-XXXXXX"));
    if (info.suffix().compare(QStringLiteral("dmg"), Qt::CaseInsensitive) == 0) {
        const QString converter = findFirstExecutable({QStringLiteral("dmg2img")});
        if (converter.isEmpty()) {
            if (error)
                *error = QStringLiteral("dmg2img is required to open DMG images.");
            return false;
        }
        if (!convertedDir.isValid()) {
            if (error)
                *error = QStringLiteral("Could not create a temporary folder.");
            return false;
        }
        const QString converted =
            convertedDir.filePath(info.completeBaseName() + QStringLiteral(".img"));
        QProcess convert;
        convert.start(converter,
                      {QStringLiteral("-i"), sourcePath, QStringLiteral("-o"), converted});
        if (!convert.waitForFinished(120000) || convert.exitCode() != 0) {
            if (error)
                *error = QString::fromLocal8Bit(convert.readAllStandardError()).trimmed();
            if (error && error->isEmpty())
                *error = QStringLiteral("Could not convert the DMG image.");
            return false;
        }
        sourcePath = converted;
    }

    QProcess setup;
    setup.setProcessChannelMode(QProcess::MergedChannels);
    setup.start(QStringLiteral("udisksctl"), {QStringLiteral("loop-setup"), QStringLiteral("-f"),
                                              sourcePath, QStringLiteral("--read-only")});
    if (!setup.waitForFinished(30000) || setup.exitCode() != 0) {
        if (error) {
            *error = QString::fromLocal8Bit(setup.readAllStandardOutput()).trimmed();
            if (error->isEmpty())
                *error = QStringLiteral("Could not create a loop device.");
        }
        return false;
    }

    const QString device = parseLoopDevice(QString::fromLocal8Bit(setup.readAllStandardOutput()));
    if (device.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not find the loop device created by udisksctl.");
        return false;
    }

    QString mountedPath;
    QString mountError;
    for (const QString& candidate : candidateMountDevices(device)) {
        mountedPath = VolumeManager::mountDevice(candidate, &mountError);
        if (!mountedPath.isEmpty())
            break;
    }
    if (mountedPath.isEmpty()) {
        detachLoop(device);
        if (error) {
            *error = mountError.isEmpty() ? QStringLiteral("Could not mount the disk image.")
                                          : mountError;
        }
        return false;
    }

    if (mountPath)
        *mountPath = mountedPath;
    if (loopDevice)
        *loopDevice = device;
    VolumeManager::invalidateCache();
    return true;
}

bool DiskImageManager::createIso(const QString& sourceDirectory, const QString& imagePath,
                                 QString* error) {
    const QFileInfo sourceInfo(sourceDirectory);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        if (error)
            *error = QStringLiteral("Select a folder to create an image from.");
        return false;
    }
    if (imagePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Choose an output image path.");
        return false;
    }

    const QString xorriso = QStandardPaths::findExecutable(QStringLiteral("xorriso"));
    const QString genisoimage = QStandardPaths::findExecutable(QStringLiteral("genisoimage"));
    const QString mkisofs = QStandardPaths::findExecutable(QStringLiteral("mkisofs"));

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (!xorriso.isEmpty()) {
        process.start(xorriso, {QStringLiteral("-as"), QStringLiteral("mkisofs"),
                                QStringLiteral("-o"), imagePath, QStringLiteral("-V"),
                                sourceInfo.fileName(), sourceInfo.absoluteFilePath()});
    } else if (!genisoimage.isEmpty()) {
        process.start(genisoimage, {QStringLiteral("-o"), imagePath, QStringLiteral("-V"),
                                    sourceInfo.fileName(), sourceInfo.absoluteFilePath()});
    } else if (!mkisofs.isEmpty()) {
        process.start(mkisofs, {QStringLiteral("-o"), imagePath, QStringLiteral("-V"),
                                sourceInfo.fileName(), sourceInfo.absoluteFilePath()});
    } else {
        if (error)
            *error = QStringLiteral("Install xorriso or genisoimage to create disk images.");
        return false;
    }

    if (!process.waitForFinished(300000) || process.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        if (error && error->isEmpty())
            *error = QStringLiteral("The disk image could not be created.");
        return false;
    }

    if (!QFileInfo::exists(imagePath)) {
        if (error)
            *error = QStringLiteral("The output image was not created.");
        return false;
    }
    return true;
}

void DiskImageManager::mountImageAsync(
    const QString& imagePath, std::function<void(bool, const QString&, const QString&)> finished) {
    if (imagePath.isEmpty()) {
        if (finished)
            finished(false, QString(), QStringLiteral("No disk image was selected."));
        return;
    }

    // A double-click while a slow mount is still running must not create a
    // second loop device for the same file.
    static QMutex mutex;
    static QHash<QString, std::shared_ptr<
                              QVector<std::function<void(bool, const QString&, const QString&)>>>>
        inFlight;
    using Callback = std::function<void(bool, const QString&, const QString&)>;

    const QString key = QFileInfo(imagePath).absoluteFilePath();
    std::shared_ptr<QVector<Callback>> waiters;
    {
        QMutexLocker locker(&mutex);
        const auto existing = inFlight.constFind(key);
        if (existing != inFlight.cend()) {
            if (finished)
                existing.value()->append(std::move(finished));
            return;
        }
        waiters = std::make_shared<QVector<Callback>>();
        if (finished)
            waiters->append(std::move(finished));
        inFlight.insert(key, waiters);
    }

    auto* watcher = new QFutureWatcher<QPair<QString, QString>>();
    QObject::connect(watcher, &QFutureWatcher<QPair<QString, QString>>::finished, watcher,
                     [watcher, key, waiters]() {
                         const QPair<QString, QString> result = watcher->result();
                         watcher->deleteLater();

                         QVector<Callback> callbacks = *waiters;
                         {
                             QMutexLocker locker(&mutex);
                             inFlight.remove(key);
                         }
                         const bool ok = !result.first.isEmpty();
                         for (const auto& callback : callbacks) {
                             if (callback)
                                 callback(ok, result.first, result.second);
                         }
                     });

    watcher->setFuture(QtConcurrent::run([key]() {
        QString mountPath;
        QString error;
        const bool ok = mountImage(key, &mountPath, nullptr, &error);
        return qMakePair(ok ? mountPath : QString(), error);
    }));
}
