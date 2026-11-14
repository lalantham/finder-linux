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
                QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
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
                QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
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
                QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("video/"))) return false;
            }
        } else if (k == QStringLiteral("audio")) {
            static const QSet<QString> audExts = {
                QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
                QStringLiteral("ogg"), QStringLiteral("m4a"), QStringLiteral("aac"),
                QStringLiteral("wma"), QStringLiteral("opus")
            };
            if (!audExts.contains(ext)) {
                QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
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
            QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
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
        // Do not follow symlinks recursively to avoid infinite recursion and filesystem loops
        flags = QDirIterator::Subdirectories;
    }

    QDirIterator it(dirPath, filters, flags);
    int checkCounter = 0;

    // Deliver results in batches to keep the event queue cheap, and cap the
    // result set so an overly broad search cannot exhaust memory.
    constexpr int BatchSize = 200;
    constexpr int MaxResults = 20000;
    QVector<FileItem> batch;
    batch.reserve(BatchSize);

    while (it.hasNext()) {
        if (m_cancelled.load(std::memory_order_relaxed)) {
            break;
        }

        it.next();
        QFileInfo fi = it.fileInfo();

        if (++checkCounter % 100 == 0) {
            emit progress(fi.dir().path());
        }

        if (matchesFilter(fi)) {
            batch.append(FileItem(fi));
            m_count++;

            if (batch.size() >= BatchSize) {
                emit itemsFound(batch);
                batch.clear();
            }
            if (m_count >= MaxResults) {
                break;
            }
        }
    }

    if (!batch.isEmpty()) {
        emit itemsFound(batch);
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
    connect(m_worker, &SearchWorker::itemsFound, this, &SearchEngine::itemsFound);
    connect(m_worker, &SearchWorker::progress, this, &SearchEngine::searchProgress);
    connect(m_worker, &SearchWorker::finished, this, [this](int total) {
        emit searchFinished(total);
        stopSearch();
    });

    m_workerThread->start();
}

void SearchEngine::stopSearch()
{
    if (!m_workerThread) {
        m_worker = nullptr;
        return;
    }

    if (m_worker) {
        m_worker->cancel();
        // Detach first so a slow worker can never deliver stale results.
        disconnect(m_worker, nullptr, this, nullptr);
    }
    disconnect(m_workerThread, nullptr, this, nullptr);

    QThread* thread = m_workerThread;
    SearchWorker* worker = m_worker;
    m_workerThread = nullptr;
    m_worker = nullptr;

    if (worker) {
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    }
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->quit();
    if (!thread->wait(1500)) {
        // Still stuck in a slow syscall (hung mount, huge directory). Do not
        // terminate() it: let it notice the cancel flag and clean itself up in
        // the background so the UI stays responsive.
        thread->setParent(nullptr);
    }
}

bool SearchEngine::isRunning() const
{
    return m_workerThread && m_workerThread->isRunning();
}
