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
    if (m_filter.query.isEmpty()) {
        return true;
    }

    Qt::CaseSensitivity cs = m_filter.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (!fi.fileName().contains(m_filter.query, cs)) {
        return false;
    }

    if (m_filter.minSize >= 0 && fi.size() < m_filter.minSize) {
        return false;
    }

    if (m_filter.maxSize >= 0 && fi.size() > m_filter.maxSize) {
        return false;
    }

    if (!m_filter.kindFilter.isEmpty()) {
        QMimeDatabase db;
        QMimeType mime = db.mimeTypeForFile(fi);
        if (!mime.name().contains(m_filter.kindFilter, Qt::CaseInsensitive) &&
            !mime.comment().contains(m_filter.kindFilter, Qt::CaseInsensitive)) {
            return false;
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
