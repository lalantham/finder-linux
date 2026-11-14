#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

struct ArchiveEntryInfo {
    QString path;
    qint64 size{0};
    bool isDir{false};
};

class ArchiveService {
public:
    static bool isArchive(const QString& filePath);
    static bool listEntries(const QString& archivePath, QVector<ArchiveEntryInfo>* entries,
                            int maxEntries = 500, QString* error = nullptr);

    static QString suggestedArchiveName(const QStringList& sources,
                                        const QString& ext = QStringLiteral("zip"));
    static QString suggestedExtractDir(const QString& archivePath);

    static bool compress(const QStringList& sources, const QString& archivePath,
                         const std::function<void(const QString& currentFile, qint64 processedBytes,
                                                  qint64 totalBytes)>& progressCallback = nullptr,
                         std::atomic<bool>* cancelFlag = nullptr, QString* error = nullptr,
                         int compressionLevel = -1, const QString& passphrase = QString());

    static bool extractAppImage(const QString& appImagePath, QString* outputDir = nullptr,
                                QString* error = nullptr);

    static bool extract(const QString& archivePath, const QString& destinationDir,
                        const std::function<void(const QString& currentFile, qint64 processedBytes,
                                                 qint64 totalBytes)>& progressCallback = nullptr,
                        std::atomic<bool>* cancelFlag = nullptr, QString* error = nullptr,
                        QString* warning = nullptr, const QString& passphrase = QString());
};
