#include "PreviewManager.h"
#include <QImageReader>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QFile>
#include <QTextStream>

#ifdef HAVE_POPPLER
#include <poppler/qt6/poppler-qt6.h>
#endif

PreviewManager& PreviewManager::instance()
{
    static PreviewManager mgr;
    return mgr;
}

PreviewManager::PreviewManager()
{
    m_thumbnailCache.setMaxCost(100 * 1024 * 1024); // 100 MB cache
}

bool PreviewManager::isImage(const QString& path) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(path);
    return mime.name().startsWith(QStringLiteral("image/"));
}

bool PreviewManager::isText(const QString& path) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(path);
    return mime.inherits(QStringLiteral("text/plain")) ||
           mime.name().startsWith(QStringLiteral("text/")) ||
           mime.name() == QStringLiteral("application/json") ||
           mime.name() == QStringLiteral("application/xml") ||
           mime.name() == QStringLiteral("application/javascript");
}

bool PreviewManager::isPdf(const QString& path) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(path);
    return mime.name() == QStringLiteral("application/pdf");
}

QPixmap PreviewManager::getThumbnail(const QString& path, const QSize& targetSize)
{
    QString cacheKey = QStringLiteral("%1_%2x%3").arg(path).arg(targetSize.width()).arg(targetSize.height());
    QPixmap* cached = m_thumbnailCache.object(cacheKey);
    if (cached) {
        return *cached;
    }

    QPixmap result;
    if (isImage(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize imageSize = reader.size();
        if (imageSize.isValid()) {
            QSize scaled = imageSize.scaled(targetSize, Qt::KeepAspectRatio);
            reader.setScaledSize(scaled);
            QImage img = reader.read();
            if (!img.isNull()) {
                result = QPixmap::fromImage(img);
            }
        }
    } else if (isPdf(path)) {
        result = getPdfPreview(path, targetSize);
    }

    if (!result.isNull()) {
        int cost = result.width() * result.height() * 4;
        m_thumbnailCache.insert(cacheKey, new QPixmap(result), cost);
    }

    return result;
}

QString PreviewManager::getTextPreview(const QString& path, int maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("Could not open file for preview.");
    }

    QByteArray data = file.read(maxBytes);
    file.close();

    QString content = QString::fromUtf8(data);
    if (file.size() > maxBytes) {
        content.append(QStringLiteral("\n\n... [Preview truncated: file is larger than %1 KB] ...").arg(maxBytes / 1024));
    }
    return content;
}

QPixmap PreviewManager::getPdfPreview(const QString& path, const QSize& targetSize)
{
#ifdef HAVE_POPPLER
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(path));
    if (!doc || doc->isLocked() || doc->numPages() == 0) {
        return {};
    }

    std::unique_ptr<Poppler::Page> page(doc->page(0));
    if (!page) {
        return {};
    }

    // Render at 150 DPI for crisp preview
    QImage img = page->renderToImage(150.0, 150.0);
    if (img.isNull()) {
        return {};
    }

    return QPixmap::fromImage(img.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
#else
    Q_UNUSED(path)
    Q_UNUSED(targetSize)
    return {};
#endif
}
