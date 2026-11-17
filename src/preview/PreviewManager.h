#pragma once

#include "FileItem.h"
#include <QCache>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <functional>
#include <memory>

#ifdef HAVE_POPPLER
namespace Poppler {
class Document;
}
#endif

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
    QImage renderPdfPageImage(const QString& path, const QSize& targetSize = QSize(600, 800),
                              int page = 0);
    QPixmap getPdfPreview(const QString& path, const QSize& targetSize = QSize(600, 800),
                          int page = 0);
    [[nodiscard]] int pdfPageCount(const QString& path) const;
    [[nodiscard]] int findPdfPage(const QString& path, const QString& text) const;

    // Asynchronous page rendering for the UI. Rendering a large page costs
    // hundreds of milliseconds, so the interactive paths must not block the
    // event loop. The callback is invoked on the UI thread with a null pixmap
    // when the page cannot be rendered. Requests for the same page supersede
    // any still-queued request for that document.
    void requestPdfPage(const QString& path, const QSize& targetSize, int page,
                        std::function<void(const QPixmap&)> finished);
    // Asynchronously counts pages, then reports the result on the UI thread.
    void requestPdfPageCount(const QString& path, std::function<void(int)> finished);
    // Asynchronously searches the document text, reporting the page index on
    // the UI thread (-1 when not found).
    void requestPdfFind(const QString& path, const QString& text,
                        std::function<void(int)> finished);

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
    [[nodiscard]] bool isFont(const QString& path) const;

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
    QImage loadImageFromDiskCache(const QString& path, int bucket);
    QPixmap loadFromDiskCache(const QString& path, int bucket);
    void saveToDiskCache(const QString& path, int bucket, const QImage& image);
    QImage generateThumbnailImage(const QString& path, int bucket);
    QString ensureOfficePdf(const QString& path, QString* error);

#ifdef HAVE_POPPLER
    // Loading a Poppler document parses the cross-reference table, which costs
    // ~130 ms on a large scan. Keep the open document so turning pages reuses it
    // instead of re-parsing the file every time. Guarded by m_pdfMutex.
    std::shared_ptr<Poppler::Document> pdfDocument(const QString& path) const;
#endif

    QCache<QString, QPixmap> m_thumbnailCache;
    QSet<QString> m_pending;
    // Thumbnail failures keyed by cache key, remembering the mtime they failed
    // at. A regenerated or replaced file gets a new key, so it is retried
    // instead of being blacklisted for the rest of the session.
    QHash<QString, qint64> m_failed;
    QSet<QString> m_officePending;

#ifdef HAVE_POPPLER
    // Guards the document cache and, because a Poppler::Document is not
    // thread-safe, also serialises rendering. Page rendering already runs on a
    // single-threaded pool, so this costs nothing in throughput.
    mutable QMutex m_pdfMutex;
    mutable QString m_cachedPdfPath;
    mutable std::shared_ptr<Poppler::Document> m_cachedPdfDocument;
#endif
    // Incremented per document so a stale in-flight request can be discarded.
    quint64 m_pdfGeneration{0};
};
