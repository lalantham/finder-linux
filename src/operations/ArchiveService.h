#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <atomic>

class ArchiveService {
public:
    static bool isArchive(const QString& filePath);

    static QString suggestedArchiveName(const QStringList& sources, const QString& ext = QStringLiteral("zip"));
    static QString suggestedExtractDir(const QString& archivePath);

    static bool compress(const QStringList& sources,
                         const QString& archivePath,
                         const std::function<void(const QString& currentFile, qint64 processedBytes, qint64 totalBytes)>& progressCallback = nullptr,
                         std::atomic<bool>* cancelFlag = nullptr,
                         QString* error = nullptr);

    static bool extract(const QString& archivePath,
                        const QString& destinationDir,
                        const std::function<void(const QString& currentFile, qint64 processedBytes, qint64 totalBytes)>& progressCallback = nullptr,
                        std::atomic<bool>* cancelFlag = nullptr,
                        QString* error = nullptr);
};
