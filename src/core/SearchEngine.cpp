#include "SearchEngine.h"
#include "TagManager.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <sys/xattr.h>

namespace {

QString readComment(const QString& path) {
    const QByteArray localPath = QFile::encodeName(path);
    char buffer[4096];
    const ssize_t length =
        getxattr(localPath.constData(), "user.xdg.comment", buffer, sizeof(buffer));
    if (length <= 0)
        return {};
    return QString::fromUtf8(buffer, static_cast<int>(length));
}

bool isTextLike(const QFileInfo& info, QMimeDatabase& mimeDb) {
    if (info.isDir())
        return false;
    const QString suffix = info.suffix().toLower();
    static const QSet<QString> textExtensions = {
        QStringLiteral("txt"),     QStringLiteral("md"),      QStringLiteral("markdown"),
        QStringLiteral("rst"),     QStringLiteral("csv"),     QStringLiteral("tsv"),
        QStringLiteral("json"),    QStringLiteral("xml"),     QStringLiteral("yaml"),
        QStringLiteral("yml"),     QStringLiteral("toml"),    QStringLiteral("ini"),
        QStringLiteral("conf"),    QStringLiteral("cfg"),     QStringLiteral("log"),
        QStringLiteral("html"),    QStringLiteral("htm"),     QStringLiteral("css"),
        QStringLiteral("scss"),    QStringLiteral("js"),      QStringLiteral("ts"),
        QStringLiteral("jsx"),     QStringLiteral("tsx"),     QStringLiteral("vue"),
        QStringLiteral("py"),      QStringLiteral("rb"),      QStringLiteral("php"),
        QStringLiteral("c"),       QStringLiteral("h"),       QStringLiteral("cc"),
        QStringLiteral("cpp"),     QStringLiteral("cxx"),     QStringLiteral("hpp"),
        QStringLiteral("hh"),      QStringLiteral("java"),    QStringLiteral("kt"),
        QStringLiteral("kts"),     QStringLiteral("go"),      QStringLiteral("rs"),
        QStringLiteral("swift"),   QStringLiteral("sh"),      QStringLiteral("bash"),
        QStringLiteral("zsh"),     QStringLiteral("fish"),    QStringLiteral("sql"),
        QStringLiteral("desktop"), QStringLiteral("service"), QStringLiteral("qml"),
        QStringLiteral("pro"),     QStringLiteral("cmake"),   QStringLiteral("diff"),
        QStringLiteral("patch"),   QStringLiteral("tex")};
    if (textExtensions.contains(suffix))
        return true;
    const QMimeType mime = mimeDb.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    return mime.name().startsWith(QStringLiteral("text/"));
}

QString extractText(const QFileInfo& info, QMimeDatabase& mimeDb) {
    const QString path = info.absoluteFilePath();
    if (info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
        static const QString pdftotext =
            QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
        if (pdftotext.isEmpty() || info.size() > 200LL * 1024LL * 1024LL)
            return {};
        QProcess process;
        process.start(pdftotext, {QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"),
                                  QStringLiteral("25"), path, QStringLiteral("-")});
        if (!process.waitForStarted(1500))
            return {};
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(500);
            return {};
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return {};
        return QString::fromUtf8(process.readAllStandardOutput());
    }

    if (!isTextLike(info, mimeDb))
        return {};
    constexpr qint64 MaxTextBytes = 4LL * 1024LL * 1024LL;
    if (info.size() > MaxTextBytes)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.read(MaxTextBytes);
    QString text = QString::fromUtf8(data);
    if (text.contains(QChar::ReplacementCharacter)) {
        text = QString::fromLocal8Bit(data);
    }
    return text;
}

bool matchesAnyTerm(const QString& haystack, const QStringList& terms, Qt::CaseSensitivity cs) {
    for (const QString& term : terms) {
        if (!haystack.contains(term, cs))
            return false;
    }
    return true;
}

struct IndexQueryResult {
    bool success{false};
    QString error;
    QStringList paths;
};

QString pathFromIndexLine(QString line) {
    line = line.trimmed();
    if (line.startsWith(QStringLiteral("file://"))) {
        return QUrl(line).toLocalFile();
    }
    if (line.startsWith(QLatin1Char('/')) && QFileInfo::exists(line)) {
        return line;
    }
    const QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        if (token.startsWith(QLatin1Char('/')) && QFileInfo::exists(token)) {
            return token;
        }
    }
    return {};
}

IndexQueryResult querySystemIndex(const QString& provider, const QString& query, int limit) {
    IndexQueryResult result;
    QString program;
    QStringList arguments;
    if (provider == QStringLiteral("Baloo")) {
        program = QStandardPaths::findExecutable(QStringLiteral("baloosearch"));
        arguments = {QStringLiteral("-l"), QString::number(limit), query};
    } else if (provider == QStringLiteral("Tracker")) {
        program = QStandardPaths::findExecutable(QStringLiteral("tracker3"));
        arguments = {QStringLiteral("search"), QStringLiteral("--files"), query};
    } else if (provider == QStringLiteral("Recoll")) {
        program = QStandardPaths::findExecutable(QStringLiteral("recoll"));
        arguments = {QStringLiteral("-t"), QStringLiteral("-b"), QStringLiteral("-n"),
                     QString::number(limit), query};
    }

    if (program.isEmpty()) {
        result.error = QStringLiteral("%1 index tool is not installed").arg(provider);
        return result;
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(2000)) {
        result.error = QStringLiteral("Could not start %1").arg(program);
        return result;
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(500);
        result.error = QStringLiteral("%1 index query timed out").arg(provider);
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (result.error.isEmpty())
            result.error = QStringLiteral("%1 index query failed").arg(provider);
        return result;
    }

    QSet<QString> seen;
    const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString path = pathFromIndexLine(line);
        if (!path.isEmpty() && !seen.contains(path)) {
            seen.insert(path);
            result.paths.append(path);
        }
    }
    result.success = true;
    return result;
}

bool isUnderSearchRoot(const SearchFilter& filter, const QString& path) {
    if (filter.thisPC)
        return true;
    QStringList roots = filter.searchPaths;
    if (roots.isEmpty() && !filter.searchPath.isEmpty())
        roots.append(filter.searchPath);
    for (const QString& root : roots) {
        if (path == root || path.startsWith(root + QLatin1Char('/')))
            return true;
    }
    return false;
}

bool shouldSkipDirectory(const QString& path, const QString& root) {
    if (root != QStringLiteral("/"))
        return false;
    static const QStringList skipped = {
        QStringLiteral("/proc"), QStringLiteral("/sys"), QStringLiteral("/dev"),
        QStringLiteral("/run"),  QStringLiteral("/tmp"), QStringLiteral("/var"),
        QStringLiteral("/usr"),  QStringLiteral("/etc"), QStringLiteral("/boot"),
        QStringLiteral("/snap"), QStringLiteral("/nix"), QStringLiteral("/home")};
    for (const QString& prefix : skipped) {
        if (path == prefix || path.startsWith(prefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

} // namespace

SearchWorker::SearchWorker(SearchFilter filter) : m_filter(std::move(filter)) {}

void SearchWorker::cancel() {
    m_cancelled.store(true, std::memory_order_relaxed);
}

bool SearchWorker::matchesFilter(const QFileInfo& fi) const {
    const QString path = fi.absoluteFilePath();
    const Qt::CaseSensitivity cs = m_filter.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (!m_filter.extensions.isEmpty() && !fi.isDir()) {
        if (!m_filter.extensions.contains(fi.suffix().toLower()))
            return false;
    } else if (!m_filter.extensions.isEmpty() && fi.isDir()) {
        return false;
    }

    if (!m_filter.kindFilter.isEmpty()) {
        const QString k = m_filter.kindFilter.toLower();
        const QString ext = fi.suffix().toLower();
        if (k == QStringLiteral("folders") || k == QStringLiteral("folder")) {
            if (!fi.isDir())
                return false;
        } else if (fi.isDir()) {
            return false;
        } else if (k == QStringLiteral("images") || k == QStringLiteral("image")) {
            static const QSet<QString> imgExts = {QStringLiteral("png"),  QStringLiteral("jpg"),
                                                  QStringLiteral("jpeg"), QStringLiteral("gif"),
                                                  QStringLiteral("svg"),  QStringLiteral("webp"),
                                                  QStringLiteral("bmp"),  QStringLiteral("ico"),
                                                  QStringLiteral("tiff"), QStringLiteral("tif")};
            if (!imgExts.contains(ext)) {
                const QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("image/")))
                    return false;
            }
        } else if (k == QStringLiteral("documents") || k == QStringLiteral("document")) {
            static const QSet<QString> docExts = {
                QStringLiteral("pdf"), QStringLiteral("doc"),  QStringLiteral("docx"),
                QStringLiteral("txt"), QStringLiteral("md"),   QStringLiteral("rtf"),
                QStringLiteral("odt"), QStringLiteral("xls"),  QStringLiteral("xlsx"),
                QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("csv")};
            if (!docExts.contains(ext)) {
                const QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("text/")) &&
                    mime.name() != QStringLiteral("application/pdf"))
                    return false;
            }
        } else if (k == QStringLiteral("videos") || k == QStringLiteral("video")) {
            static const QSet<QString> vidExts = {QStringLiteral("mp4"),  QStringLiteral("mkv"),
                                                  QStringLiteral("avi"),  QStringLiteral("mov"),
                                                  QStringLiteral("webm"), QStringLiteral("wmv"),
                                                  QStringLiteral("flv"),  QStringLiteral("m4v")};
            if (!vidExts.contains(ext)) {
                const QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("video/")))
                    return false;
            }
        } else if (k == QStringLiteral("audio")) {
            static const QSet<QString> audExts = {QStringLiteral("mp3"), QStringLiteral("flac"),
                                                  QStringLiteral("wav"), QStringLiteral("ogg"),
                                                  QStringLiteral("m4a"), QStringLiteral("aac"),
                                                  QStringLiteral("wma"), QStringLiteral("opus")};
            if (!audExts.contains(ext)) {
                const QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
                if (!mime.name().startsWith(QStringLiteral("audio/")))
                    return false;
            }
        } else if (k == QStringLiteral("archives") || k == QStringLiteral("archive")) {
            static const QSet<QString> arcExts = {QStringLiteral("zip"), QStringLiteral("tar"),
                                                  QStringLiteral("gz"),  QStringLiteral("bz2"),
                                                  QStringLiteral("xz"),  QStringLiteral("7z"),
                                                  QStringLiteral("rar"), QStringLiteral("tgz")};
            if (!arcExts.contains(ext))
                return false;
        } else {
            const QMimeType mime = m_mimeDb.mimeTypeForFile(fi);
            if (!mime.name().contains(k, Qt::CaseInsensitive) &&
                !mime.comment().contains(k, Qt::CaseInsensitive))
                return false;
        }
    }

    if (m_filter.minSize >= 0 || m_filter.maxSize >= 0) {
        if (fi.isDir())
            return false;
        if (m_filter.minSize >= 0 && fi.size() < m_filter.minSize)
            return false;
        if (m_filter.maxSize >= 0 && fi.size() > m_filter.maxSize)
            return false;
    }

    if (m_filter.modifiedFrom.isValid() || m_filter.modifiedTo.isValid()) {
        const QDate modified = fi.lastModified().date();
        if (m_filter.modifiedFrom.isValid() && modified < m_filter.modifiedFrom)
            return false;
        if (m_filter.modifiedTo.isValid() && modified > m_filter.modifiedTo)
            return false;
    }

    const bool filenameMatches =
        m_filter.nameTerms.isEmpty() || matchesAnyTerm(fi.fileName(), m_filter.nameTerms, cs);

    QStringList tags;
    QString comment;
    const bool needTagFilter = !m_filter.tags.isEmpty();
    const bool needMetadata = needTagFilter || !m_filter.excludeTerms.isEmpty() ||
                              (!filenameMatches && !m_filter.nameTerms.isEmpty());
    if (needMetadata) {
        // Memory-first lookup avoids an xattr syscall for every file on every
        // search. Cold probes are bounded so large trees cannot stall a scan.
        tags = TagManager::instance().cachedTagsForFile(path);
        constexpr int MetadataProbeBudget = 50000;
        if (needTagFilter || (!m_filter.excludeTerms.isEmpty()) ||
            (tags.isEmpty() && m_metadataProbes < MetadataProbeBudget)) {
            tags = TagManager::instance().tagsForFile(path);
            ++m_metadataProbes;
        }
        if (!m_filter.excludeTerms.isEmpty() || !filenameMatches) {
            comment = readComment(path);
        }
    }

    if (needTagFilter) {
        for (const QString& wanted : m_filter.tags) {
            bool found = false;
            for (const QString& tag : tags) {
                if (tag.compare(wanted, Qt::CaseInsensitive) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
    }

    const QString metadataHaystack = fi.fileName() + QLatin1Char('\n') +
                                     tags.join(QLatin1Char('\n')) + QLatin1Char('\n') + comment;
    const bool nameMatches = m_filter.nameTerms.isEmpty() || filenameMatches ||
                             matchesAnyTerm(metadataHaystack, m_filter.nameTerms, cs);
    if (!m_filter.excludeTerms.isEmpty()) {
        for (const QString& term : m_filter.excludeTerms) {
            if (metadataHaystack.contains(term, cs))
                return false;
        }
    }

    const bool contentSearch = m_filter.searchContents && !m_filter.contentTerms.isEmpty();
    if (!nameMatches && !contentSearch)
        return false;

    if (contentSearch) {
        if (fi.isDir()) {
            if (!nameMatches)
                return false;
        } else {
            const QString content = extractText(fi, m_mimeDb);
            const bool contentMatches =
                !content.isEmpty() && matchesAnyTerm(content, m_filter.contentTerms, cs);
            // A UI "Contents" search accepts a name/tag/comment match as well;
            // an explicit content: token with no name terms requires content.
            if (!contentMatches && (!nameMatches || m_filter.nameTerms.isEmpty()))
                return false;
        }
    }

    return true;
}

void SearchWorker::searchDirectory(const QString& dirPath, const QString& rootPath) {
    if (m_cancelled.load(std::memory_order_relaxed))
        return;

    QDir dir(dirPath);
    QDir::Filters filters = QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System;
    if (m_filter.includeHidden) {
        filters |= QDir::Hidden;
    }

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::NoSort);
    if (++m_progressCounter % 25 == 1) {
        emit progress(dirPath);
    }

    constexpr int BatchSize = 200;
    constexpr int MaxResults = 20000;
    static thread_local QVector<FileItem> batch;
    if (batch.isEmpty())
        batch.reserve(BatchSize);

    for (const QFileInfo& fi : entries) {
        if (m_cancelled.load(std::memory_order_relaxed))
            break;
        if (m_count >= MaxResults)
            break;

        if (fi.isDir() && !fi.isSymLink() && m_filter.recursive &&
            !shouldSkipDirectory(fi.absoluteFilePath(), rootPath)) {
            searchDirectory(fi.absoluteFilePath(), rootPath);
        }

        if (matchesFilter(fi)) {
            batch.append(FileItem(fi));
            ++m_count;
            if (batch.size() >= BatchSize) {
                emit itemsFound(batch);
                batch.clear();
            }
        }
    }

    if (!batch.isEmpty()) {
        emit itemsFound(batch);
        batch.clear();
    }
}

void SearchWorker::run() {
    const QString provider = SearchEngine::availableIndexProvider();
    if (m_filter.useIndex && !provider.isEmpty()) {
        QString query = m_filter.nameTerms.join(QLatin1Char(' '));
        if (query.isEmpty())
            query = m_filter.contentTerms.join(QLatin1Char(' '));
        if (!query.isEmpty()) {
            const IndexQueryResult indexed = querySystemIndex(provider, query, 5000);
            if (indexed.success) {
                QVector<FileItem> batch;
                for (const QString& path : indexed.paths) {
                    if (m_cancelled.load(std::memory_order_relaxed))
                        break;
                    if (!isUnderSearchRoot(m_filter, path))
                        continue;
                    const QFileInfo info(path);
                    if (matchesFilter(info)) {
                        batch.append(FileItem(info));
                        ++m_count;
                        if (batch.size() >= 200) {
                            emit itemsFound(batch);
                            batch.clear();
                        }
                    }
                }
                if (!batch.isEmpty())
                    emit itemsFound(batch);
                emit finished(m_count);
                return;
            }
            // If the index is unavailable or failed, fall back to the full scan.
        }
    }

    QStringList roots;
    if (m_filter.thisPC) {
        roots = m_filter.searchPaths.isEmpty() ? SearchEngine::localSearchRoots()
                                               : m_filter.searchPaths;
    } else {
        roots = m_filter.searchPaths;
        if (roots.isEmpty() && !m_filter.searchPath.isEmpty()) {
            roots.append(m_filter.searchPath);
        }
    }

    QSet<QString> seen;
    for (const QString& root : roots) {
        if (m_cancelled.load(std::memory_order_relaxed))
            break;
        const QString clean = QDir::cleanPath(root);
        if (clean.isEmpty() || !QDir(clean).exists() || seen.contains(clean))
            continue;
        seen.insert(clean);
        searchDirectory(clean, clean);
    }
    emit finished(m_count);
}

// SearchEngine
SearchEngine::SearchEngine(QObject* parent) : QObject(parent) {}

SearchEngine::~SearchEngine() {
    stopSearch();
}

QString SearchEngine::availableIndexProvider() {
    if (!QStandardPaths::findExecutable(QStringLiteral("baloosearch")).isEmpty()) {
        return QStringLiteral("Baloo");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("tracker3")).isEmpty()) {
        return QStringLiteral("Tracker");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("recoll")).isEmpty()) {
        return QStringLiteral("Recoll");
    }
    return {};
}

QStringList SearchEngine::localSearchRoots() {
    QStringList roots{QDir::homePath()};

    static const QSet<QString> remoteTypes = {
        QStringLiteral("nfs"),         QStringLiteral("nfs4"),
        QStringLiteral("cifs"),        QStringLiteral("smb3"),
        QStringLiteral("smbfs"),       QStringLiteral("afs"),
        QStringLiteral("ceph"),        QStringLiteral("9p"),
        QStringLiteral("davfs"),       QStringLiteral("fuse.sshfs"),
        QStringLiteral("fuse.rclone"), QStringLiteral("fuse.gvfsd-fuse"),
        QStringLiteral("proc"),        QStringLiteral("sysfs"),
        QStringLiteral("devtmpfs"),    QStringLiteral("tmpfs"),
        QStringLiteral("overlay"),     QStringLiteral("squashfs")};

    for (const QStorageInfo& storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady())
            continue;
        const QString fsType = QString::fromLatin1(storage.fileSystemType()).toLower();
        if (remoteTypes.contains(fsType))
            continue;
        const QString path = QDir::cleanPath(storage.rootPath());
        if (!path.isEmpty() && !roots.contains(path)) {
            roots.append(path);
        }
    }

    // The root filesystem is searched last; its system directories are pruned
    // while walking so this stays practical without a system index.
    if (!roots.contains(QStringLiteral("/"))) {
        roots.append(QStringLiteral("/"));
    }
    return roots;
}

void SearchEngine::startSearch(const SearchFilter& filter) {
    stopSearch();

    // Backwards compatibility for callers that populate only filter.query.
    SearchFilter normalized = filter;
    if (!normalized.query.isEmpty() && normalized.nameTerms.isEmpty() &&
        normalized.contentTerms.isEmpty()) {
        const SearchFilter parsed = SearchQuery::parse(normalized.query);
        normalized.nameTerms = parsed.nameTerms;
        normalized.excludeTerms = parsed.excludeTerms;
        normalized.contentTerms = parsed.contentTerms;
        if (normalized.kindFilter.isEmpty())
            normalized.kindFilter = parsed.kindFilter;
        if (normalized.tags.isEmpty())
            normalized.tags = parsed.tags;
        if (normalized.extensions.isEmpty())
            normalized.extensions = parsed.extensions;
        if (normalized.minSize < 0)
            normalized.minSize = parsed.minSize;
        if (normalized.maxSize < 0)
            normalized.maxSize = parsed.maxSize;
        if (!normalized.modifiedFrom.isValid())
            normalized.modifiedFrom = parsed.modifiedFrom;
        if (!normalized.modifiedTo.isValid())
            normalized.modifiedTo = parsed.modifiedTo;
        normalized.searchContents = normalized.searchContents || parsed.searchContents;
    }

    m_workerThread = new QThread(this);
    m_worker = new SearchWorker(normalized);
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

void SearchEngine::stopSearch() {
    if (!m_workerThread) {
        m_worker = nullptr;
        return;
    }

    if (m_worker) {
        m_worker->cancel();
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

    // Do not block the UI waiting for a slow scan or a child conversion
    // process. The worker observes the cancel flag and cleans itself up when
    // the thread eventually finishes.
    thread->setParent(nullptr);
    thread->quit();
}

bool SearchEngine::isRunning() const {
    return m_workerThread && m_workerThread->isRunning();
}
