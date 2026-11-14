#pragma once

#include "FileItem.h"
#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

class PreviewManager : public QObject {
    Q_OBJECT

public:
    static PreviewManager& instance();

    // Blocking lookup/generation (used by preview panes and gallery).
    QPixmap getThumbnail(const QString& path, const QSize& targetSize = QSize(128, 128));
    // Non-blocking lookup for delegates: returns a cached thumbnail, or queues
    // generation in the background and returns a null pixmap for now.
    QPixmap iconThumbnail(const QString& path, int iconSize);

    QString getTextPreview(const QString& path, int maxBytes = 64 * 1024);
    QPixmap getPdfPreview(const QString& path, const QSize& targetSize = QSize(600, 800),
                          int page = 0);
    [[nodiscard]] int pdfPageCount(const QString& path) const;
    // Convert an Office document to a cached PDF on demand. Blocking; use
    // requestOfficePreview() from UI code to keep the event loop responsive.
    QString officePdfPath(const QString& path, QString* error = nullptr);
    void requestOfficePreview(const QString& path);

    [[nodiscard]] bool isImage(const QString& path) const;
    [[nodiscard]] bool isText(const QString& path) const;
    [[nodiscard]] bool isPdf(const QString& path) const;
    [[nodiscard]] bool isVideo(const QString& path) const;
    [[nodiscard]] bool isAudio(const QString& path) const;
    [[nodiscard]] bool isOffice(const QString& path) const;

signals:
    void thumbnailReady(const QString& path);
    void officePreviewReady(const QString& sourcePath, const QString& pdfPath,
                            const QString& error);

private:
    PreviewManager();
    ~PreviewManager() override = default;

    static int bucketForSize(int size);
    static QString bucketDirectory(int bucket);
    static bool isThumbnailCandidate(const QString& path);

    QString cacheFilePath(const QString& path, int bucket) const;
    QPixmap loadFromDiskCache(const QString& path, int bucket);
    void saveToDiskCache(const QString& path, int bucket, const QImage& image);
    QImage generateThumbnailImage(const QString& path, int bucket);
    QString ensureOfficePdf(const QString& path, QString* error);

    QCache<QString, QPixmap> m_thumbnailCache;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    QSet<QString> m_officePending;
};
