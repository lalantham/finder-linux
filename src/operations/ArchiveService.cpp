#include "ArchiveService.h"
#include <archive.h>
#include <archive_entry.h>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSet>
#include <fcntl.h>
#include <unistd.h>

bool ArchiveService::isArchive(const QString& filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    QString fileName = QFileInfo(filePath).fileName().toLower();
    if (fileName.endsWith(QStringLiteral(".tar.gz")) ||
        fileName.endsWith(QStringLiteral(".tar.xz")) ||
        fileName.endsWith(QStringLiteral(".tar.bz2")) ||
        fileName.endsWith(QStringLiteral(".tar.zst")) ||
        fileName.endsWith(QStringLiteral(".tar.lz4"))) {
        return true;
    }
    static const QSet<QString> archiveExts = {
        QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("tgz"),
        QStringLiteral("txz"), QStringLiteral("tbz2"), QStringLiteral("7z"),
        QStringLiteral("rar"), QStringLiteral("iso"), QStringLiteral("cpio"),
        QStringLiteral("gz"), QStringLiteral("bz2"), QStringLiteral("xz"),
        QStringLiteral("zst")
    };
    return archiveExts.contains(ext);
}

QString ArchiveService::suggestedArchiveName(const QStringList& sources, const QString& ext)
{
    if (sources.isEmpty()) return QStringLiteral("Archive.%1").arg(ext);

    QFileInfo fi(sources.first());
    QString parentDir = fi.absolutePath();
    QString baseName;
    if (sources.size() == 1) {
        baseName = fi.fileName();
    } else {
        baseName = QStringLiteral("Archive");
    }

    QString candidate = QDir(parentDir).filePath(QStringLiteral("%1.%2").arg(baseName, ext));
    int counter = 2;
    while (QFile::exists(candidate)) {
        candidate = QDir(parentDir).filePath(QStringLiteral("%1 %2.%3").arg(baseName).arg(counter).arg(ext));
        counter++;
    }
    return candidate;
}

QString ArchiveService::suggestedExtractDir(const QString& archivePath)
{
    QFileInfo fi(archivePath);
    QString parentDir = fi.absolutePath();
    QString baseName = fi.completeBaseName();
    if (baseName.endsWith(QStringLiteral(".tar"), Qt::CaseInsensitive)) {
        baseName.chop(4);
    }
    if (baseName.isEmpty()) baseName = QStringLiteral("Extracted");

    QString candidate = QDir(parentDir).filePath(baseName);
    int counter = 2;
    while (QDir(candidate).exists() || QFile::exists(candidate)) {
        candidate = QDir(parentDir).filePath(QStringLiteral("%1 %2").arg(baseName).arg(counter));
        counter++;
    }
    return candidate;
}

bool ArchiveService::compress(const QStringList& sources,
                             const QString& archivePath,
                             const std::function<void(const QString& currentFile, qint64 processedBytes, qint64 totalBytes)>& progressCallback,
                             std::atomic<bool>* cancelFlag,
                             QString* error)
{
    if (sources.isEmpty()) {
        if (error) *error = QStringLiteral("No files to compress.");
        return false;
    }

    // 1. Calculate total bytes for progress
    qint64 totalBytes = 0;
    for (const auto& src : sources) {
        QFileInfo fi(src);
        if (fi.isDir()) {
            QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                if (it.fileInfo().isFile()) {
                    totalBytes += it.fileInfo().size();
                }
            }
        } else {
            totalBytes += fi.size();
        }
    }

    // 2. Setup libarchive writer
    struct archive* a = archive_write_new();
    QString lowerPath = archivePath.toLower();

    if (lowerPath.endsWith(QStringLiteral(".zip"))) {
        archive_write_set_format_zip(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.gz")) || lowerPath.endsWith(QStringLiteral(".tgz"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_gzip(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.xz")) || lowerPath.endsWith(QStringLiteral(".txz"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_xz(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.bz2")) || lowerPath.endsWith(QStringLiteral(".tbz2"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_bzip2(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.zst"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_zstd(a);
    } else if (lowerPath.endsWith(QStringLiteral(".7z"))) {
        archive_write_set_format_7zip(a);
    } else {
        // Default to ZIP
        archive_write_set_format_zip(a);
    }

    int openStatus = archive_write_open_filename(a, archivePath.toUtf8().constData());
    if (openStatus != ARCHIVE_OK) {
        if (error) *error = QString::fromUtf8(archive_error_string(a));
        archive_write_free(a);
        return false;
    }

    // 3. Process entries
    QFileInfo firstFi(sources.first());
    QString baseParent = firstFi.absolutePath();
    if (!baseParent.endsWith(QLatin1Char('/'))) {
        baseParent += QLatin1Char('/');
    }

    qint64 processedBytes = 0;
    char buffer[65536];

    auto addSingleFile = [&](const QString& fullPath, const QString& relPath) -> bool {
        if (cancelFlag && cancelFlag->load()) return false;

        QFileInfo fi(fullPath);
        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, relPath.toUtf8().constData());
        archive_entry_set_size(entry, fi.size());
        archive_entry_set_mtime(entry, fi.lastModified().toSecsSinceEpoch(), 0);

        if (fi.isSymLink()) {
            archive_entry_set_filetype(entry, AE_IFLNK);
            archive_entry_set_symlink(entry, fi.symLinkTarget().toUtf8().constData());
            archive_write_header(a, entry);
            archive_entry_free(entry);
        } else if (fi.isDir()) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_perm(entry, 0755);
            archive_write_header(a, entry);
            archive_entry_free(entry);
        } else {
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_write_header(a, entry);

            QFile file(fullPath);
            if (file.open(QIODevice::ReadOnly)) {
                while (!file.atEnd()) {
                    if (cancelFlag && cancelFlag->load()) {
                        archive_entry_free(entry);
                        return false;
                    }
                    qint64 bytesRead = file.read(buffer, sizeof(buffer));
                    if (bytesRead > 0) {
                        archive_write_data(a, buffer, static_cast<size_t>(bytesRead));
                        processedBytes += bytesRead;
                        if (progressCallback) {
                            progressCallback(relPath, processedBytes, totalBytes);
                        }
                    }
                }
                file.close();
            }
            archive_entry_free(entry);
        }
        return true;
    };

    for (const auto& src : sources) {
        if (cancelFlag && cancelFlag->load()) break;

        QFileInfo fi(src);
        QString relPath = fi.fileName();

        if (fi.isDir()) {
            // Add root directory of this entry
            if (!addSingleFile(src, relPath + QLatin1Char('/'))) {
                break;
            }

            QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (cancelFlag && cancelFlag->load()) break;
                it.next();
                QString childFull = it.filePath();
                QString childRel = childFull;
                if (childRel.startsWith(baseParent)) {
                    childRel = childRel.mid(baseParent.length());
                }
                if (it.fileInfo().isDir() && !childRel.endsWith(QLatin1Char('/'))) {
                    childRel += QLatin1Char('/');
                }
                if (!addSingleFile(childFull, childRel)) {
                    break;
                }
            }
        } else {
            if (!addSingleFile(src, relPath)) {
                break;
            }
        }
    }

    archive_write_close(a);
    archive_write_free(a);

    if (cancelFlag && cancelFlag->load()) {
        QFile::remove(archivePath);
        if (error) *error = QStringLiteral("Operation cancelled.");
        return false;
    }

    return true;
}

bool ArchiveService::extract(const QString& archivePath,
                            const QString& destinationDir,
                            const std::function<void(const QString& currentFile, qint64 processedBytes, qint64 totalBytes)>& progressCallback,
                            std::atomic<bool>* cancelFlag,
                            QString* error)
{
    if (!QFileInfo::exists(archivePath)) {
        if (error) *error = QStringLiteral("Archive file does not exist.");
        return false;
    }

    struct archive* r = archive_read_new();
    archive_read_support_format_all(r);
    archive_read_support_filter_all(r);

    int status = archive_read_open_filename(r, archivePath.toUtf8().constData(), 16384);
    if (status != ARCHIVE_OK) {
        if (error) *error = QString::fromUtf8(archive_error_string(r));
        archive_read_free(r);
        return false;
    }

    struct archive* ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS);

    QDir().mkpath(destinationDir);
    QString cleanTarget = QDir::cleanPath(destinationDir);

    struct archive_entry* entry;
    qint64 processedBytes = 0;
    bool success = true;

    while (archive_read_next_header(r, &entry) == ARCHIVE_OK) {
        if (cancelFlag && cancelFlag->load()) {
            success = false;
            break;
        }

        QString entryPath = QString::fromUtf8(archive_entry_pathname(entry));
        // Zip slip vulnerability prevention:
        QString fullPath = QDir::cleanPath(QDir(cleanTarget).filePath(entryPath));
        if (!fullPath.startsWith(cleanTarget + QLatin1Char('/')) && fullPath != cleanTarget) {
            // Path traversal attempt, skip entry for security!
            continue;
        }

        archive_entry_set_pathname(entry, fullPath.toUtf8().constData());
        int hStatus = archive_write_header(ext, entry);
        if (hStatus != ARCHIVE_OK) {
            continue;
        }

        const void* buff;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(r, &buff, &size, &offset) == ARCHIVE_OK) {
            archive_write_data_block(ext, buff, size, offset);
            processedBytes += static_cast<qint64>(size);
        }
        archive_write_finish_entry(ext);

        if (progressCallback) {
            progressCallback(entryPath, processedBytes, 0);
        }
    }

    archive_write_close(ext);
    archive_write_free(ext);
    archive_read_close(r);
    archive_read_free(r);

    if (cancelFlag && cancelFlag->load()) {
        if (error) *error = QStringLiteral("Extraction cancelled.");
        return false;
    }

    return success;
}
