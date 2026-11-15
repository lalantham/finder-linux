#include "ArchiveService.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <archive.h>
#include <archive_entry.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <thread>

namespace {

void waitWhileArchivePaused(std::atomic<bool>* pauseFlag, std::atomic<bool>* cancelFlag) {
    while (pauseFlag && pauseFlag->load(std::memory_order_relaxed) &&
           !(cancelFlag && cancelFlag->load(std::memory_order_relaxed))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

} // namespace

bool ArchiveService::isArchive(const QString& filePath) {
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
        QStringLiteral("zip"),  QStringLiteral("tar"), QStringLiteral("tgz"), QStringLiteral("txz"),
        QStringLiteral("tbz2"), QStringLiteral("7z"),  QStringLiteral("rar"), QStringLiteral("iso"),
        QStringLiteral("cpio"), QStringLiteral("gz"),  QStringLiteral("bz2"), QStringLiteral("xz"),
        QStringLiteral("zst")};
    return archiveExts.contains(ext);
}

bool ArchiveService::listEntries(const QString& archivePath, QVector<ArchiveEntryInfo>* entries,
                                 int maxEntries, QString* error) {
    if (!entries)
        return false;
    entries->clear();
    struct archive* archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);
    if (archive_read_open_filename(archive, QFile::encodeName(archivePath).constData(), 10240) !=
        ARCHIVE_OK) {
        if (error)
            *error = QString::fromLocal8Bit(archive_error_string(archive));
        archive_read_free(archive);
        return false;
    }

    struct archive_entry* entry = nullptr;
    int result = ARCHIVE_OK;
    int count = 0;
    while ((result = archive_read_next_header(archive, &entry)) == ARCHIVE_OK &&
           count < maxEntries) {
        ArchiveEntryInfo info;
        const char* name = archive_entry_pathname(entry);
        info.path = name ? QString::fromUtf8(name) : QString();
        info.size = archive_entry_size(entry);
        info.isDir = archive_entry_filetype(entry) == AE_IFDIR;
        if (!info.path.isEmpty()) {
            entries->append(info);
            ++count;
        }
        archive_read_data_skip(archive);
    }

    const bool ok = result == ARCHIVE_EOF || result == ARCHIVE_OK || count >= maxEntries;
    if (!ok && error)
        *error = QString::fromLocal8Bit(archive_error_string(archive));
    archive_read_free(archive);
    return ok;
}

QString ArchiveService::suggestedArchiveName(const QStringList& sources, const QString& ext) {
    if (sources.isEmpty())
        return QStringLiteral("Archive.%1").arg(ext);

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
        candidate = QDir(parentDir).filePath(
            QStringLiteral("%1 %2.%3").arg(baseName).arg(counter).arg(ext));
        counter++;
    }
    return candidate;
}

QString ArchiveService::suggestedExtractDir(const QString& archivePath) {
    QFileInfo fi(archivePath);
    QString parentDir = fi.absolutePath();
    QString baseName = fi.completeBaseName();
    if (baseName.endsWith(QStringLiteral(".tar"), Qt::CaseInsensitive)) {
        baseName.chop(4);
    }
    if (baseName.isEmpty())
        baseName = QStringLiteral("Extracted");

    QString candidate = QDir(parentDir).filePath(baseName);
    int counter = 2;
    while (QDir(candidate).exists() || QFile::exists(candidate)) {
        candidate = QDir(parentDir).filePath(QStringLiteral("%1 %2").arg(baseName).arg(counter));
        counter++;
    }
    return candidate;
}

bool ArchiveService::compress(
    const QStringList& sources, const QString& archivePath,
    const std::function<void(const QString& currentFile, qint64 processedBytes, qint64 totalBytes)>&
        progressCallback,
    std::atomic<bool>* cancelFlag, QString* error, int compressionLevel,
    const QString& passphrase, std::atomic<bool>* pauseFlag) {
    if (sources.isEmpty()) {
        if (error)
            *error = QStringLiteral("No files to compress.");
        return false;
    }

    // 1. Calculate total bytes for progress
    qint64 totalBytes = 0;
    for (const auto& src : sources) {
        QFileInfo fi(src);
        if (fi.isDir() && !fi.isSymLink()) {
            QDirIterator it(src,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
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

    // 2. Setup libarchive writer for the requested format.
    const QString lowerPath = archivePath.toLower();
    struct archive* a = archive_write_new();

    if (lowerPath.endsWith(QStringLiteral(".zip"))) {
        archive_write_set_format_zip(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.gz")) ||
               lowerPath.endsWith(QStringLiteral(".tgz"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_gzip(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.xz")) ||
               lowerPath.endsWith(QStringLiteral(".txz"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_xz(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.bz2")) ||
               lowerPath.endsWith(QStringLiteral(".tbz2"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_bzip2(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar.zst"))) {
        archive_write_set_format_pax_restricted(a);
        archive_write_add_filter_zstd(a);
    } else if (lowerPath.endsWith(QStringLiteral(".7z"))) {
        archive_write_set_format_7zip(a);
    } else if (lowerPath.endsWith(QStringLiteral(".tar"))) {
        archive_write_set_format_pax_restricted(a);
    } else {
        if (error)
            *error = QStringLiteral("Unsupported archive format: %1")
                         .arg(QFileInfo(archivePath).suffix());
        archive_write_free(a);
        return false;
    }

    if (compressionLevel > 0) {
        const QByteArray level = QByteArray::number(compressionLevel);
        if (lowerPath.endsWith(QStringLiteral(".zip"))) {
            const QByteArray option = QByteArrayLiteral("zip:compression-level=") + level;
            archive_write_set_options(a, option.constData());
        } else if (!lowerPath.endsWith(QStringLiteral(".7z")) &&
                   !lowerPath.endsWith(QStringLiteral(".tar"))) {
            archive_write_set_filter_option(a, nullptr, "compression-level", level.constData());
        }
    }

    if (!passphrase.isEmpty()) {
        if (!lowerPath.endsWith(QStringLiteral(".zip"))) {
            if (error)
                *error = QStringLiteral("Password protection is only supported for ZIP archives.");
            archive_write_free(a);
            return false;
        }
        int encryptionStatus = archive_write_set_options(a, "zip:encryption=aes256");
        if (encryptionStatus != ARCHIVE_OK) {
            encryptionStatus = archive_write_set_options(a, "zip:encryption=zipcrypt");
        }
        if (encryptionStatus != ARCHIVE_OK ||
            archive_write_set_passphrase(a, passphrase.toUtf8().constData()) != ARCHIVE_OK) {
            if (error)
                *error = QStringLiteral("Could not enable ZIP encryption.");
            archive_write_free(a);
            return false;
        }
    }

    // Never write into the target archive directly: an existing archive must
    // survive a cancelled or failed compression.
    const QString tempPath =
        archivePath +
        QStringLiteral(".part-%1-%2").arg(::getpid()).arg(QRandomGenerator::global()->generate());

    int openStatus = archive_write_open_filename(a, QFile::encodeName(tempPath).constData());
    if (openStatus != ARCHIVE_OK) {
        if (error)
            *error = QString::fromUtf8(archive_error_string(a));
        archive_write_free(a);
        QFile::remove(tempPath);
        return false;
    }

    qint64 processedBytes = 0;
    char buffer[65536];

    auto addEntry = [&](const QString& fullPath, const QString& relPath) -> bool {
        waitWhileArchivePaused(pauseFlag, cancelFlag);
        if (cancelFlag && cancelFlag->load())
            return false;

        struct stat st{};
        if (::lstat(QFile::encodeName(fullPath).constData(), &st) != 0) {
            if (error) {
                *error = QStringLiteral("Cannot read %1: %2")
                             .arg(fullPath, QString::fromLocal8Bit(std::strerror(errno)));
            }
            return false;
        }

        struct archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, relPath.toUtf8().constData());
        archive_entry_set_mtime(entry, st.st_mtime, 0);
        archive_entry_set_perm(entry, st.st_mode & 07777);

        bool ok = true;
        if (S_ISLNK(st.st_mode)) {
            archive_entry_set_filetype(entry, AE_IFLNK);
            archive_entry_set_size(entry, 0);
            archive_entry_set_symlink(entry,
                                      QFileInfo(fullPath).symLinkTarget().toUtf8().constData());
            if (archive_write_header(a, entry) != ARCHIVE_OK) {
                if (error)
                    *error = QString::fromUtf8(archive_error_string(a));
                ok = false;
            }
        } else if (S_ISDIR(st.st_mode)) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_size(entry, 0);
            if (archive_write_header(a, entry) != ARCHIVE_OK) {
                if (error)
                    *error = QString::fromUtf8(archive_error_string(a));
                ok = false;
            }
        } else if (S_ISREG(st.st_mode)) {
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_size(entry, st.st_size);
            if (archive_write_header(a, entry) != ARCHIVE_OK) {
                if (error)
                    *error = QString::fromUtf8(archive_error_string(a));
                ok = false;
            } else {
                QFile file(fullPath);
                if (!file.open(QIODevice::ReadOnly)) {
                    // Abort rather than emitting a header for data we cannot supply.
                    if (error)
                        *error =
                            QStringLiteral("Cannot read %1: %2").arg(fullPath, file.errorString());
                    ok = false;
                } else {
                    while (ok && !file.atEnd()) {
                        waitWhileArchivePaused(pauseFlag, cancelFlag);
                        if (cancelFlag && cancelFlag->load()) {
                            ok = false;
                            break;
                        }
                        qint64 bytesRead = file.read(buffer, sizeof(buffer));
                        if (bytesRead < 0) {
                            if (error)
                                *error = QStringLiteral("Cannot read %1: %2")
                                             .arg(fullPath, file.errorString());
                            ok = false;
                            break;
                        }
                        if (bytesRead == 0)
                            break;
                        if (archive_write_data(a, buffer, static_cast<size_t>(bytesRead)) !=
                            bytesRead) {
                            if (error)
                                *error = QString::fromUtf8(archive_error_string(a));
                            ok = false;
                            break;
                        }
                        processedBytes += bytesRead;
                        if (progressCallback) {
                            progressCallback(relPath, processedBytes, totalBytes);
                        }
                    }
                }
            }
        } else {
            // FIFOs/sockets/devices would block or stream forever while reading.
            if (error)
                *error =
                    QStringLiteral("%1 is a special file and cannot be archived.").arg(fullPath);
            ok = false;
        }

        archive_entry_free(entry);
        return ok;
    };

    bool ok = true;
    for (const auto& src : sources) {
        waitWhileArchivePaused(pauseFlag, cancelFlag);
        if (cancelFlag && cancelFlag->load()) {
            ok = false;
            break;
        }

        QFileInfo fi(src);
        QString baseParent = fi.absolutePath();
        if (!baseParent.endsWith(QLatin1Char('/'))) {
            baseParent += QLatin1Char('/');
        }
        QString relPath = fi.fileName();

        if (fi.isDir() && !fi.isSymLink()) {
            if (!addEntry(src, relPath + QLatin1Char('/'))) {
                ok = false;
                break;
            }

            QDirIterator it(src,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (cancelFlag && cancelFlag->load()) {
                    ok = false;
                    break;
                }
                it.next();
                QString childFull = it.filePath();
                QString childRel = childFull;
                if (childRel.startsWith(baseParent)) {
                    childRel = childRel.mid(baseParent.length());
                }
                if (it.fileInfo().isDir() && !it.fileInfo().isSymLink() &&
                    !childRel.endsWith(QLatin1Char('/'))) {
                    childRel += QLatin1Char('/');
                }
                if (!addEntry(childFull, childRel)) {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                break;
        } else {
            if (!addEntry(src, relPath)) {
                ok = false;
                break;
            }
        }
    }

    const int closeStatus = archive_write_close(a);
    archive_write_free(a);

    if (cancelFlag && cancelFlag->load()) {
        QFile::remove(tempPath);
        if (error && error->isEmpty())
            *error = QStringLiteral("Operation cancelled.");
        return false;
    }
    if (!ok) {
        QFile::remove(tempPath);
        if (error && error->isEmpty())
            *error = QStringLiteral("Compression failed.");
        return false;
    }
    if (closeStatus != ARCHIVE_OK) {
        QFile::remove(tempPath);
        if (error)
            *error = QStringLiteral("Failed to finalise the archive.");
        return false;
    }

    // Atomically replace (or create) the archive only after a complete write.
    if (::rename(QFile::encodeName(tempPath).constData(),
                 QFile::encodeName(archivePath).constData()) != 0) {
        const QString reason = QString::fromLocal8Bit(std::strerror(errno));
        QFile::remove(tempPath);
        if (error)
            *error = QStringLiteral("Could not create %1: %2").arg(archivePath, reason);
        return false;
    }

    return true;
}

bool ArchiveService::extractAppImage(const QString& appImagePath, QString* outputDir,
                                     QString* error) {
    const QFileInfo info(appImagePath);
    if (!info.exists() || info.isDir()) {
        if (error)
            *error = QStringLiteral("%1 is not an AppImage file.").arg(appImagePath);
        return false;
    }

    const QString source = info.absoluteFilePath();
    if (!info.isExecutable()) {
        QFile::Permissions permissions = info.permissions();
        permissions |= QFileDevice::ExeOwner;
        if (permissions & QFileDevice::ReadGroup) {
            permissions |= QFileDevice::ExeGroup;
        }
        if (permissions & QFileDevice::ReadOther) {
            permissions |= QFileDevice::ExeOther;
        }
        if (!QFile::setPermissions(source, permissions) || !QFileInfo(source).isExecutable()) {
            if (error) {
                *error = QStringLiteral("%1 is not executable and could not be made executable.")
                             .arg(info.fileName());
            }
            return false;
        }
    }

    QString target = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                     QStringLiteral("-extracted");
    int counter = 2;
    while (QFileInfo::exists(target)) {
        target = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                 QStringLiteral("-%1").arg(counter++);
    }

    const QString tempDirectory =
        info.absolutePath() + QLatin1Char('/') +
        QStringLiteral(".finder-appimage-%1").arg(QRandomGenerator::global()->generate());
    if (!QDir().mkpath(tempDirectory)) {
        if (error)
            *error = QStringLiteral("Could not create a temporary extraction folder.");
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(tempDirectory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(source, {QStringLiteral("--appimage-extract")});
    if (!process.waitForStarted(10000)) {
        if (error) {
            *error = QStringLiteral("%1: could not start extraction: %2")
                         .arg(info.fileName(), process.errorString());
        }
        QDir(tempDirectory).removeRecursively();
        return false;
    }
    process.waitForFinished(-1);

    const QString extracted = tempDirectory + QStringLiteral("/squashfs-root");
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !QDir(extracted).exists()) {
        QString detail = QString::fromUtf8(process.readAll()).trimmed();
        if (detail.isEmpty()) {
            detail = QStringLiteral("AppImage extraction failed.");
        }
        if (error)
            *error = QStringLiteral("%1: %2").arg(info.fileName(), detail.left(400));
        QDir(tempDirectory).removeRecursively();
        return false;
    }

    if (!QDir().rename(extracted, target)) {
        if (error) {
            *error = QStringLiteral("%1: could not move the extracted contents to %2.")
                         .arg(info.fileName(), target);
        }
        QDir(tempDirectory).removeRecursively();
        return false;
    }

    QDir(tempDirectory).removeRecursively();
    if (outputDir)
        *outputDir = target;
    return true;
}

bool ArchiveService::extract(
    const QString& archivePath, const QString& destinationDir,
    const std::function<void(const QString& currentFile, qint64 processedBytes, qint64 totalBytes)>&
        progressCallback,
    std::atomic<bool>* cancelFlag, QString* error, QString* warning, const QString& passphrase,
    std::atomic<bool>* pauseFlag, const std::function<bool(const QString&)>& shouldSkip) {
    if (!QFileInfo::exists(archivePath)) {
        if (error)
            *error = QStringLiteral("Archive file does not exist.");
        return false;
    }

    struct archive* r = archive_read_new();
    archive_read_support_format_all(r);
    archive_read_support_filter_all(r);
    if (!passphrase.isEmpty()) {
        archive_read_add_passphrase(r, passphrase.toUtf8().constData());
    }

    int status = archive_read_open_filename(r, QFile::encodeName(archivePath).constData(), 16384);
    if (status != ARCHIVE_OK) {
        if (error)
            *error = QString::fromUtf8(archive_error_string(r));
        archive_read_free(r);
        return false;
    }

    struct archive* ext = archive_write_disk_new();
    archive_write_disk_set_options(
        ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                 ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_NO_OVERWRITE);

    QDir().mkpath(destinationDir);
    QString cleanTarget = QDir::cleanPath(destinationDir);

    struct archive_entry* entry = nullptr;
    qint64 processedBytes = 0;
    bool success = true;
    int skippedExisting = 0;
    QString failureMessage;

    auto fail = [&](const QString& message) {
        success = false;
        if (failureMessage.isEmpty())
            failureMessage = message;
    };

    while (true) {
        waitWhileArchivePaused(pauseFlag, cancelFlag);
        const int headerStatus = archive_read_next_header(r, &entry);
        if (headerStatus == ARCHIVE_EOF) {
            break;
        }
        if (headerStatus != ARCHIVE_OK) {
            fail(QStringLiteral("Corrupted archive: %1")
                     .arg(QString::fromUtf8(archive_error_string(r))));
            break;
        }
        if (cancelFlag && cancelFlag->load()) {
            fail(QStringLiteral("Extraction cancelled."));
            break;
        }

        QString rawEntryPath = QString::fromUtf8(archive_entry_pathname(entry));
        // Strip leading slashes and drive letters (prevent absolute path extraction)
        while (rawEntryPath.startsWith(QLatin1Char('/')) ||
               rawEntryPath.startsWith(QLatin1Char('\\'))) {
            rawEntryPath.remove(0, 1);
        }
        if (shouldSkip && shouldSkip(rawEntryPath))
            continue;

        // Zip slip / path traversal prevention
        QString fullPath = QDir::cleanPath(QDir(cleanTarget).filePath(rawEntryPath));
        if (!fullPath.startsWith(cleanTarget + QLatin1Char('/')) && fullPath != cleanTarget) {
            continue;
        }

        // Validate symlink targets so they cannot escape the destination
        if (archive_entry_filetype(entry) == AE_IFLNK) {
            const char* symlinkTargetRaw = archive_entry_symlink(entry);
            if (symlinkTargetRaw) {
                QString symlinkTarget = QString::fromUtf8(symlinkTargetRaw);
                QString resolvedTarget;
                if (QDir::isAbsolutePath(symlinkTarget)) {
                    resolvedTarget = QDir::cleanPath(symlinkTarget);
                } else {
                    resolvedTarget =
                        QDir::cleanPath(QFileInfo(fullPath).dir().filePath(symlinkTarget));
                }
                if (!resolvedTarget.startsWith(cleanTarget + QLatin1Char('/')) &&
                    resolvedTarget != cleanTarget) {
                    continue;
                }
            }
        }

        const bool isDirEntry = archive_entry_filetype(entry) == AE_IFDIR;
        struct stat existing{};
        const bool destinationExists =
            ::lstat(QFile::encodeName(fullPath).constData(), &existing) == 0;
        if (destinationExists) {
            if (isDirEntry && S_ISDIR(existing.st_mode)) {
                continue; // existing folder: merge without touching it
            }
            // Never overwrite existing data during extraction.
            skippedExisting++;
            continue;
        }

        archive_entry_set_pathname(entry, QFile::encodeName(fullPath).constData());
        const int writeHeaderStatus = archive_write_header(ext, entry);
        if (writeHeaderStatus != ARCHIVE_OK) {
            fail(QStringLiteral("Could not extract %1: %2")
                     .arg(rawEntryPath, QString::fromUtf8(archive_error_string(ext))));
            break;
        }

        bool entryFailed = false;
        while (true) {
            waitWhileArchivePaused(pauseFlag, cancelFlag);
            if (cancelFlag && cancelFlag->load()) {
                fail(QStringLiteral("Extraction cancelled."));
                entryFailed = true;
                break;
            }
            const void* buff = nullptr;
            size_t size = 0;
            la_int64_t offset = 0;
            const int readStatus = archive_read_data_block(r, &buff, &size, &offset);
            if (readStatus == ARCHIVE_EOF) {
                break;
            }
            if (readStatus != ARCHIVE_OK) {
                fail(QStringLiteral("Corrupted archive entry %1: %2")
                         .arg(rawEntryPath, QString::fromUtf8(archive_error_string(r))));
                entryFailed = true;
                break;
            }
            if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK) {
                fail(QStringLiteral("Could not write %1: %2")
                         .arg(rawEntryPath, QString::fromUtf8(archive_error_string(ext))));
                entryFailed = true;
                break;
            }
            processedBytes += static_cast<qint64>(size);
        }

        if (entryFailed)
            break;

        if (archive_write_finish_entry(ext) != ARCHIVE_OK) {
            fail(QStringLiteral("Could not finalise %1: %2")
                     .arg(rawEntryPath, QString::fromUtf8(archive_error_string(ext))));
            break;
        }

        if (progressCallback) {
            progressCallback(rawEntryPath, processedBytes, 0);
        }
    }

    archive_write_close(ext);
    archive_write_free(ext);
    archive_read_close(r);
    archive_read_free(r);

    if (cancelFlag && cancelFlag->load()) {
        if (error)
            *error = QStringLiteral("Extraction cancelled.");
        return false;
    }

    if (warning && skippedExisting > 0) {
        *warning = QStringLiteral("%1 existing item(s) were left untouched and not overwritten.")
                       .arg(skippedExisting);
    }

    if (!success) {
        if (error)
            *error =
                failureMessage.isEmpty() ? QStringLiteral("Extraction failed.") : failureMessage;
        return false;
    }

    return true;
}
