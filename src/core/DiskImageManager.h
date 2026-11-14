#pragma once

#include <QString>

class DiskImageManager {
public:
    [[nodiscard]] static bool isDiskImage(const QString& path);
    [[nodiscard]] static QString fileDialogFilter();

    // Mount an ISO/IMG/RAW/SquashFS image through a UDisks2 loop device.
    // DMG images are converted first when dmg2img is installed.
    static bool mountImage(const QString& imagePath, QString* mountPath = nullptr,
                           QString* loopDevice = nullptr, QString* error = nullptr);

    // Create an ISO image from a directory using xorriso, genisoimage, or mkisofs.
    static bool createIso(const QString& sourceDirectory, const QString& imagePath,
                          QString* error = nullptr);
};
