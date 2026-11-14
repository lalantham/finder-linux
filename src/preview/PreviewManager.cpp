#include "PreviewManager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

#ifdef HAVE_POPPLER
#include <poppler/qt6/poppler-qt6.h>
#endif

namespace {

constexpr int NormalBucket = 128;
constexpr int LargeBucket = 256;
constexpr int XLargeBucket = 512;
constexpr int XXLargeBucket = 1024;

const QSet<QString>& officeExtensions() {
    static const QSet<QString> exts = {
        QStringLiteral("doc"),  QStringLiteral("docx"), QStringLiteral("docm"),
        QStringLiteral("dot"),  QStringLiteral("dotx"), QStringLiteral("odt"),
        QStringLiteral("ott"),  QStringLiteral("rtf"),  QStringLiteral("xls"),
        QStringLiteral("xlsx"), QStringLiteral("xlsm"), QStringLiteral("xlt"),
        QStringLiteral("xltx"), QStringLiteral("ods"),  QStringLiteral("ots"),
        QStringLiteral("ppt"),  QStringLiteral("pptx"), QStringLiteral("pptm"),
        QStringLiteral("pps"),  QStringLiteral("ppsx"), QStringLiteral("odp"),
        QStringLiteral("otp"),  QStringLiteral("vsd"),  QStringLiteral("vsdx")};
    return exts;
}

QString officeCacheDirectory() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.cache");
    }
    return base + QStringLiteral("/file-manager/office-previews");
}

QString officeCachePathFor(const QString& path) {
    const QFileInfo info(path);
    const QString key = info.absoluteFilePath() + QLatin1Char('\n') +
                        QString::number(info.lastModified().toMSecsSinceEpoch()) +
                        QLatin1Char('\n') + QString::number(info.size());
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return officeCacheDirectory() + QLatin1Char('/') + QString::fromLatin1(hash) +
           QStringLiteral(".pdf");
}

void cleanupOfficeCache() {
    QDir dir(officeCacheDirectory());
    if (!dir.exists())
        return;
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Time | QDir::Reversed);
    const int maxFiles = 256;
    if (files.size() > maxFiles) {
        for (int i = 0; i < files.size() - maxFiles; ++i) {
            QFile::remove(files.at(i).absoluteFilePath());
        }
    }
}

#ifdef HAVE_POPPLER
QImage renderPdfFirstPage(const QString& path, int bucket) {
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(path));
    if (!doc || doc->isLocked() || doc->numPages() == 0)
        return {};
    std::unique_ptr<Poppler::Page> page(doc->page(0));
    if (!page)
        return {};
    const QImage rendered = page->renderToImage(150.0, 150.0);
    if (rendered.isNull())
        return {};
    return rendered.scaled(QSize(bucket, bucket), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
#endif

QString thumbnailUri(const QString& path) {
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

QString baseCacheDirectory() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.cache");
    }
    return base + QStringLiteral("/thumbnails");
}

quint32 readBigEndian32(const QByteArray& data, qsizetype offset) {
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24) |
           (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16) |
           (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8) |
           static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

// QImageReader does not reliably expose PNG tEXt chunks, so parse them here to
// validate the XDG thumbnail cache (Thumb::URI / Thumb::MTime).
QHash<QString, QString> readPngTextChunks(const QString& filePath) {
    QHash<QString, QString> result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const QByteArray data = file.readAll();
    if (!data.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1a\n"))) {
        return result;
    }

    qsizetype offset = 8;
    while (offset + 12 <= data.size()) {
        const quint32 length = readBigEndian32(data, offset);
        const QByteArray type = data.mid(offset + 4, 4);
        const qsizetype payloadOffset = offset + 8;
        if (payloadOffset + static_cast<qsizetype>(length) + 4 > data.size()) {
            break;
        }

        if (type == QByteArrayLiteral("tEXt")) {
            const QByteArray payload = data.mid(payloadOffset, static_cast<qsizetype>(length));
            const qsizetype separator = payload.indexOf('\0');
            if (separator > 0) {
                result.insert(QString::fromLatin1(payload.left(separator)),
                              QString::fromLatin1(payload.mid(separator + 1)));
            }
        }

        offset = payloadOffset + static_cast<qsizetype>(length) + 4;
        if (type == QByteArrayLiteral("IEND")) {
            break;
        }
    }
    return result;
}

} // namespace

PreviewManager& PreviewManager::instance() {
    static PreviewManager mgr;
    return mgr;
}

PreviewManager::PreviewManager() {
    m_thumbnailCache.setMaxCost(100 * 1024 * 1024); // 100 MB cache
}

bool PreviewManager::isImage(const QString& path) const {
    static const QSet<QString> imageExts = {QStringLiteral("png"),  QStringLiteral("jpg"),
                                            QStringLiteral("jpeg"), QStringLiteral("gif"),
                                            QStringLiteral("bmp"),  QStringLiteral("webp"),
                                            QStringLiteral("tif"),  QStringLiteral("tiff"),
                                            QStringLiteral("ico"),  QStringLiteral("svg")};
    if (imageExts.contains(QFileInfo(path).suffix().toLower())) {
        return true;
    }
    QMimeDatabase db;
    return db.mimeTypeForFile(path, QMimeDatabase::MatchExtension)
        .name()
        .startsWith(QStringLiteral("image/"));
}

bool PreviewManager::isText(const QString& path) const {
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(path);
    return mime.inherits(QStringLiteral("text/plain")) ||
           mime.name().startsWith(QStringLiteral("text/")) ||
           mime.name() == QStringLiteral("application/json") ||
           mime.name() == QStringLiteral("application/xml") ||
           mime.name() == QStringLiteral("application/javascript");
}

bool PreviewManager::isPdf(const QString& path) const {
    return QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
}

bool PreviewManager::isVideo(const QString& path) const {
    static const QSet<QString> videoExts = {
        QStringLiteral("mp4"), QStringLiteral("mkv"),  QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"),
        QStringLiteral("flv"), QStringLiteral("m4v"),  QStringLiteral("ts"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ogv")};
    if (videoExts.contains(QFileInfo(path).suffix().toLower())) {
        return true;
    }
    QMimeDatabase db;
    return db.mimeTypeForFile(path, QMimeDatabase::MatchExtension)
        .name()
        .startsWith(QStringLiteral("video/"));
}

bool PreviewManager::isOffice(const QString& path) const {
    const QFileInfo info(path);
    if (!info.exists() || info.isDir())
        return false;
    if (officeExtensions().contains(info.suffix().toLower()))
        return true;

    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    const QString name = mime.name();
    return name == QStringLiteral("application/msword") ||
           name == QStringLiteral("application/rtf") || name == QStringLiteral("text/rtf") ||
           name == QStringLiteral("application/vnd.ms-excel") ||
           name == QStringLiteral("application/vnd.ms-powerpoint") ||
           name.startsWith(QStringLiteral("application/vnd.oasis.opendocument.")) ||
           name.startsWith(QStringLiteral("application/vnd.openxmlformats-officedocument.")) ||
           name.startsWith(QStringLiteral("application/vnd.ms-"));
}

bool PreviewManager::isAudio(const QString& path) const {
    static const QSet<QString> audioExts = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("ogg"),
        QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("wma"), QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("ape"),
        QStringLiteral("mka")};
    if (audioExts.contains(QFileInfo(path).suffix().toLower())) {
        return true;
    }
    QMimeDatabase db;
    return db.mimeTypeForFile(path, QMimeDatabase::MatchExtension)
        .name()
        .startsWith(QStringLiteral("audio/"));
}

bool PreviewManager::isThumbnailCandidate(const QString& path) {
    static const QSet<QString> exts = {
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("gif"),  QStringLiteral("bmp"),  QStringLiteral("webp"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("ico"),
        QStringLiteral("svg"),  QStringLiteral("pdf"),  QStringLiteral("mp4"),
        QStringLiteral("mkv"),  QStringLiteral("avi"),  QStringLiteral("mov"),
        QStringLiteral("webm"), QStringLiteral("wmv"),  QStringLiteral("flv"),
        QStringLiteral("m4v"),  QStringLiteral("ts"),   QStringLiteral("mpg"),
        QStringLiteral("mpeg"), QStringLiteral("ogv")};
    // Office documents are converted only on demand (Preview), never
    // automatically for every visible grid item.
    return exts.contains(QFileInfo(path).suffix().toLower());
}

int PreviewManager::bucketForSize(int size) {
    if (size <= NormalBucket)
        return NormalBucket;
    if (size <= LargeBucket)
        return LargeBucket;
    if (size <= XLargeBucket)
        return XLargeBucket;
    return XXLargeBucket;
}

QString PreviewManager::bucketDirectory(int bucket) {
    switch (bucket) {
    case NormalBucket:
        return QStringLiteral("normal");
    case LargeBucket:
        return QStringLiteral("large");
    case XLargeBucket:
        return QStringLiteral("x-large");
    default:
        return QStringLiteral("xx-large");
    }
}

QString PreviewManager::cacheFilePath(const QString& path, int bucket) const {
    const QByteArray hash =
        QCryptographicHash::hash(thumbnailUri(path).toUtf8(), QCryptographicHash::Md5).toHex();
    return baseCacheDirectory() + QLatin1Char('/') + bucketDirectory(bucket) + QLatin1Char('/') +
           QString::fromLatin1(hash) + QStringLiteral(".png");
}

QPixmap PreviewManager::loadFromDiskCache(const QString& path, int bucket) {
    const QString cachePath = cacheFilePath(path, bucket);
    if (!QFile::exists(cachePath)) {
        return {};
    }

    const QHash<QString, QString> text = readPngTextChunks(cachePath);
    const QFileInfo info(path);
    if (text.value(QStringLiteral("Thumb::URI")) != thumbnailUri(path) ||
        text.value(QStringLiteral("Thumb::MTime")) !=
            QString::number(info.lastModified().toSecsSinceEpoch())) {
        return {};
    }

    QImageReader reader(cachePath);
    const QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    return QPixmap::fromImage(image);
}

void PreviewManager::saveToDiskCache(const QString& path, int bucket, const QImage& image) {
    const QString cachePath = cacheFilePath(path, bucket);
    if (!QDir().mkpath(QFileInfo(cachePath).absolutePath())) {
        return;
    }

    QImage thumbnail = image;
    thumbnail.setText(QStringLiteral("Thumb::URI"), thumbnailUri(path));
    thumbnail.setText(QStringLiteral("Thumb::MTime"),
                      QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()));
    thumbnail.save(cachePath, "PNG");
}

QString PreviewManager::ensureOfficePdf(const QString& path, QString* error) {
    const QString cached = officeCachePathFor(path);
    if (QFileInfo(cached).size() > 0) {
        return cached;
    }

    QString program = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    if (program.isEmpty()) {
        program = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    }
    if (program.isEmpty()) {
        if (error)
            *error = QStringLiteral("LibreOffice is not installed.");
        return {};
    }

    static QMutex conversionMutex;
    QMutexLocker locker(&conversionMutex);

    // Another request may have produced the PDF while this one waited.
    if (QFileInfo(cached).size() > 0) {
        return cached;
    }

    if (!QDir().mkpath(officeCacheDirectory())) {
        if (error)
            *error = QStringLiteral("Could not create the document preview cache.");
        return {};
    }

    QTemporaryDir outputDir(QDir::tempPath() + QStringLiteral("/file-manager-office-XXXXXX"));
    if (!outputDir.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create a temporary output folder.");
        return {};
    }

    const QString profileDir = officeCacheDirectory() + QStringLiteral("/lo-profile");
    QDir().mkpath(profileDir);

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
        program,
        {QStringLiteral("--headless"), QStringLiteral("--norestore"),
         QStringLiteral("--nofirststartwizard"), QStringLiteral("--nolockcheck"),
         QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(profileDir).toString(),
         QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"),
         outputDir.path(), QFileInfo(path).absoluteFilePath()});

    if (!process.waitForStarted(3000)) {
        if (error)
            *error = QStringLiteral("Could not start LibreOffice.");
        return {};
    }
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error)
            *error = QStringLiteral("Document conversion timed out.");
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString details = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        if (error) {
            *error = details.isEmpty()
                         ? QStringLiteral("LibreOffice could not convert this document.")
                         : details;
        }
        return {};
    }

    const QStringList pdfs =
        QDir(outputDir.path()).entryList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Name);
    if (pdfs.isEmpty()) {
        if (error)
            *error = QStringLiteral("LibreOffice did not produce a PDF preview.");
        return {};
    }

    const QString generated = outputDir.filePath(pdfs.first());
    if (!QFile::copy(generated, cached)) {
        if (error)
            *error = QStringLiteral("Could not cache the document preview.");
        return {};
    }
    cleanupOfficeCache();
    return cached;
}

QString PreviewManager::officePdfPath(const QString& path, QString* error) {
    return ensureOfficePdf(path, error);
}

void PreviewManager::requestOfficePreview(const QString& path) {
    if (path.isEmpty() || m_officePending.contains(path))
        return;
    m_officePending.insert(path);

    (void)QtConcurrent::run([this, path]() {
        QString error;
        const QString pdfPath = ensureOfficePdf(path, &error);
        QMetaObject::invokeMethod(
            this,
            [this, path, pdfPath, error]() {
                m_officePending.remove(path);
                emit officePreviewReady(path, pdfPath, error);
            },
            Qt::QueuedConnection);
    });
}

QImage PreviewManager::generateThumbnailImage(const QString& path, int bucket) {
    QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }

    if (isVideo(path)) {
        static const QString ffmpegThumb =
            QStandardPaths::findExecutable(QStringLiteral("ffmpegthumbnailer"));
        if (!ffmpegThumb.isEmpty()) {
            QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/finder_vid_XXXXXX.png"));
            tempFile.setAutoRemove(true);
            if (tempFile.open()) {
                QString tempPath = tempFile.fileName();
                tempFile.close();
                QProcess proc;
                proc.start(ffmpegThumb,
                           {QStringLiteral("-i"), path, QStringLiteral("-o"), tempPath,
                            QStringLiteral("-s"), QString::number(bucket), QStringLiteral("-c"),
                            QStringLiteral("png"), QStringLiteral("-t"), QStringLiteral("10%")});
                if (proc.waitForFinished(4000) && proc.exitStatus() == QProcess::NormalExit &&
                    proc.exitCode() == 0) {
                    QImage img(tempPath);
                    if (!img.isNull()) {
                        return img;
                    }
                }
            }
        }
        return {};
    }

    if (info.size() > 100 * 1024 * 1024) {
        return {};
    }

    if (isPdf(path)) {
#ifdef HAVE_POPPLER
        return renderPdfFirstPage(path, bucket);
#else
        return {};
#endif
    }

    if (isOffice(path)) {
        QString error;
        const QString pdfPath = ensureOfficePdf(path, &error);
        if (pdfPath.isEmpty())
            return {};
#ifdef HAVE_POPPLER
        return renderPdfFirstPage(pdfPath, bucket);
#else
        Q_UNUSED(bucket)
        return {};
#endif
    }

    if (!isImage(path)) {
        return {};
    }

    QImageReader reader(path);
    reader.setAllocationLimit(128);
    reader.setAutoTransform(true);
    const QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        reader.setScaledSize(imageSize.scaled(QSize(bucket, bucket), Qt::KeepAspectRatio));
    }
    return reader.read();
}

QPixmap PreviewManager::getThumbnail(const QString& path, const QSize& targetSize) {
    if (!isThumbnailCandidate(path)) {
        return {};
    }

    const int bucket = bucketForSize(qMax(targetSize.width(), targetSize.height()));
    const QString cacheKey =
        QStringLiteral("%1_%2x%3").arg(path).arg(targetSize.width()).arg(targetSize.height());
    if (QPixmap* cached = m_thumbnailCache.object(cacheKey)) {
        return *cached;
    }

    QPixmap thumbnail = loadFromDiskCache(path, bucket);
    if (thumbnail.isNull()) {
        const QImage image = generateThumbnailImage(path, bucket);
        if (image.isNull()) {
            return {};
        }
        saveToDiskCache(path, bucket, image);
        thumbnail = QPixmap::fromImage(image);
    }

    if (thumbnail.isNull()) {
        return {};
    }

    m_thumbnailCache.insert(cacheKey, new QPixmap(thumbnail),
                            thumbnail.width() * thumbnail.height() * 4);
    return thumbnail.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap PreviewManager::iconThumbnail(const QString& path, int iconSize) {
    if (iconSize < 48 || !isThumbnailCandidate(path)) {
        return {};
    }

    const int bucket = bucketForSize(iconSize);
    const QString cacheKey = QStringLiteral("%1#thumb%2").arg(path).arg(bucket);

    if (QPixmap* cached = m_thumbnailCache.object(cacheKey)) {
        return *cached;
    }

    QPixmap thumbnail = loadFromDiskCache(path, bucket);
    if (!thumbnail.isNull()) {
        m_thumbnailCache.insert(cacheKey, new QPixmap(thumbnail),
                                thumbnail.width() * thumbnail.height() * 4);
        return thumbnail;
    }

    if (m_pending.contains(cacheKey) || m_failed.contains(cacheKey)) {
        return {};
    }
    m_pending.insert(cacheKey);

    (void)QtConcurrent::run([this, path, bucket, cacheKey]() {
        const QImage image = generateThumbnailImage(path, bucket);
        if (!image.isNull()) {
            saveToDiskCache(path, bucket, image);
        }

        QMetaObject::invokeMethod(
            this,
            [this, path, cacheKey, image]() {
                m_pending.remove(cacheKey);
                if (image.isNull()) {
                    m_failed.insert(cacheKey);
                    return;
                }
                QPixmap pixmap = QPixmap::fromImage(image);
                m_thumbnailCache.insert(cacheKey, new QPixmap(pixmap),
                                        pixmap.width() * pixmap.height() * 4);
                emit thumbnailReady(path);
            },
            Qt::QueuedConnection);
    });

    return {};
}

QString PreviewManager::getTextPreview(const QString& path, int maxBytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("Could not open file for preview.");
    }

    QByteArray data = file.read(maxBytes);
    file.close();

    QString content = QString::fromUtf8(data);
    if (file.size() > maxBytes) {
        content.append(QStringLiteral("\n\n... [Preview truncated: file is larger than %1 KB] ...")
                           .arg(maxBytes / 1024));
    }
    return content;
}

QPixmap PreviewManager::getPdfPreview(const QString& path, const QSize& targetSize, int page) {
#ifdef HAVE_POPPLER
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(path));
    if (!doc || doc->isLocked() || doc->numPages() == 0) {
        return {};
    }
    if (page < 0 || page >= doc->numPages()) {
        return {};
    }

    std::unique_ptr<Poppler::Page> documentPage(doc->page(page));
    if (!documentPage) {
        return {};
    }

    QImage img = documentPage->renderToImage(150.0, 150.0);
    if (img.isNull()) {
        return {};
    }

    return QPixmap::fromImage(
        img.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
#else
    Q_UNUSED(path)
    Q_UNUSED(targetSize)
    Q_UNUSED(page)
    return {};
#endif
}

int PreviewManager::pdfPageCount(const QString& path) const {
#ifdef HAVE_POPPLER
    std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(path));
    if (!doc || doc->isLocked())
        return 0;
    return doc->numPages();
#else
    Q_UNUSED(path)
    return 0;
#endif
}
