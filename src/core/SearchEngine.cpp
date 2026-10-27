#include "SearchEngine.h"
#include <QDir>
#include <QDirIterator>
#include <QSet>

SearchWorker::SearchWorker(SearchFilter filter)
    : m_filter(std::move(filter))
{
}

void SearchWorker::cancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

bool SearchWorker::matchesFilter(const QFileInfo& fi) const
{
    if (!m_filter.query.isEmpty()) {
        Qt::CaseSensitivity cs = m_filter.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (!fi.fileName().contains(m_filter.query, cs)) {
            return false;
        }
    }

    if (m_filter.minSize >= 0 && fi.size() < m_filter.minSize) {
        return false;
    }

    if (m_filter.maxSize >= 0 && fi.size() > m_filter.maxSize) {
        return false;
    }

    if (!m_filter.kindFilter.isEmpty()) {
        QString k = m_filter.kindFilter.toLower();
        QString ext = fi.suffix().toLower();
        if (k == QStringLiteral("folders") || k == QStringLiteral("folder")) {
            if (!fi.isDir()) return false;
        } else if (fi.isDir()) {
            return false;
        } else if (k == QStringLiteral("images") || k == QStringLiteral("image")) {
            static const QSet<QString> imgExts = {
                QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
                QStringLiteral("bmp"), QStringLiteral("ico"), QStringLiteral("tiff"), QStringLiteral("tif")
            };
            if (!imgExts.contains(ext)) {
                QMimeDatabase db;
                QMimeType mime = db.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("image/"))) return false;
            }
        } else if (k == QStringLiteral("documents") || k == QStringLiteral("document")) {
            static const QSet<QString> docExts = {
                QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"),
                QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("rtf"),
                QStringLiteral("odt"), QStringLiteral("xls"), QStringLiteral("xlsx"),
                QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("csv")
            };
            if (!docExts.contains(ext)) {
                QMimeDatabase db;
                QMimeType mime = db.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("text/")) &&
                    mime.name() != QStringLiteral("application/pdf")) return false;
            }
        } else if (k == QStringLiteral("videos") || k == QStringLiteral("video")) {
            static const QSet<QString> vidExts = {
                QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
                QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("wmv"),
                QStringLiteral("flv"), QStringLiteral("m4v")
            };
            if (!vidExts.contains(ext)) {
                QMimeDatabase db;
                QMimeType mime = db.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("video/"))) return false;
            }
        } else if (k == QStringLiteral("audio")) {
            static const QSet<QString> audExts = {
                QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
                QStringLiteral("ogg"), QStringLiteral("m4a"), QStringLiteral("aac"),
                QStringLiteral("wma"), QStringLiteral("opus")
            };
            if (!audExts.contains(ext)) {
                QMimeDatabase db;
                QMimeType mime = db.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("audio/"))) return false;
            }
        } else if (k == QStringLiteral("archives") || k == QStringLiteral("archive")) {
            static const QSet<QString> arcExts = {
                QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"),
                QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("7z"),
                QStringLiteral("rar"), QStringLiteral("tgz")
            };
            if (!arcExts.contains(ext)) return false;
        } else {
            QMimeDatabase db;
            QMimeType mime = db.mimeTypeForFile(fi);
            if (!mime.name().contains(k, Qt::CaseInsensitive) &&
                !mime.comment().contains(k, Qt::CaseInsensitive)) {
                return false;
            }
        }
    }

    return true;
}

void SearchWorker::searchDirectory(const QString& dirPath)
{
    QDir::Filters filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System;
    if (m_filter.includeHidden) {
        filters |= QDir::Hidden;
    }

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (m_filter.recursive) {
        flags = QDirIterator::Subdirectories | QDirIterator::FollowSymlinks;
    }

    QDirIterator it(dirPath, filters, flags);
    QSet<QString> visitedDirs;
    int checkCounter = 0;

    while (it.hasNext()) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            break;
        }

        it.next();
        QFileInfo fi = it.fileInfo();

        // Symlink loop prevention
        if (fi.isDir() && fi.isSymLink()) {
            QString canonical = fi.canonicalFilePath();
            if (visitedDirs.contains(canonical)) {
                continue;
            }
            visitedDirs.insert(canonical);
        }

        if (++checkCounter % 100 == 0) {
            emit progress(fi.dir().path());
        }

        if (matchesFilter(fi)) {
            FileItem item(fi);
            emit itemFound(item);
            m_count++;
        }
    }
}

void SearchWorker::run()
{
    searchDirectory(m_filter.searchPath);
    emit finished(m_count);
}

// SearchEngine
SearchEngine::SearchEngine(QObject* parent)
    : QObject(parent)
{
}

SearchEngine::~SearchEngine()
{
    stopSearch();
}

void SearchEngine::startSearch(const SearchFilter& filter)
{
    stopSearch();

    m_workerThread = new QThread(this);
    m_worker = new SearchWorker(filter);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &SearchWorker::run);
    connect(m_worker, &SearchWorker::itemFound, this, &SearchEngine::itemFound);
    connect(m_worker, &SearchWorker::progress, this, &SearchEngine::searchProgress);
    connect(m_worker, &SearchWorker::finished, this, [this](int total) {
        emit searchFinished(total);
        stopSearch();
    });

    m_workerThread->start();
}

void SearchEngine::stopSearch()
{
    if (m_worker) {
        m_worker->cancel();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(300);
        if (m_workerThread->isRunning()) {
            m_workerThread->terminate();
            m_workerThread->wait();
        }
        delete m_worker;
        delete m_workerThread;
        m_worker = nullptr;
        m_workerThread = nullptr;
    }
}

bool SearchEngine::isRunning() const
{
    return m_workerThread && m_workerThread->isRunning();
}
