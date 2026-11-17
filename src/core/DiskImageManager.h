#pragma once

#include <QString>
#include <functional>

class DiskImageManager {
public:
    [[nodiscard]] static bool isDiskImage(const QString& path);
    [[nodiscard]] static QString fileDialogFilter();

    // Mount an ISO/IMG/RAW/SquashFS image through a UDisks2 loop device.
    // DMG images are converted first when dmg2img is installed.
    // Blocking: call only from a worker thread.
    static bool mountImage(const QString& imagePath, QString* mountPath = nullptr,
                           QString* loopDevice = nullptr, QString* error = nullptr);

    // Create an ISO image from a directory using xorriso, genisoimage, or mkisofs.
    // Blocking: call only from a worker thread.
    static bool createIso(const QString& sourceDirectory, const QString& imagePath,
                          QString* error = nullptr);

    // Mounts off the GUI thread. The callback runs on the UI thread once the
    // mount finishes. Repeated requests for an image already being mounted are
    // coalesced instead of spawning redundant loop devices.
    static void mountImageAsync(
        const QString& imagePath,
        std::function<void(bool ok, const QString& mountPath, const QString& error)> finished);
};
