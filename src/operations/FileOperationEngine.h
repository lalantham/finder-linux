#pragma once

#include "FileOperation.h"
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <functional>

class FileOperationEngine : public QObject {
    Q_OBJECT

public:
    explicit FileOperationEngine(QObject* parent = nullptr);
    ~FileOperationEngine() override;

    // Returns false when another operation is already running.
    bool copyItems(const QStringList& sources, const QString& destinationDir,
                   ConflictResolution res = ConflictResolution::Ask);
    bool moveItems(const QStringList& sources, const QString& destinationDir,
                   ConflictResolution res = ConflictResolution::Ask);
    bool duplicateItems(const QStringList& sources);
    bool trashItems(const QStringList& sources);
    bool deletePermanently(const QStringList& sources);
    bool createFolder(const QString& parentDir, const QString& folderName,
                      QString* createdPath = nullptr, QString* error = nullptr);
    bool createFile(const QString& parentDir, const QString& fileName,
                    QString* createdPath = nullptr, QString* error = nullptr);
    bool renameItem(const QString& sourcePath, const QString& newName,
                    QString* renamedPath = nullptr, QString* error = nullptr);
    bool createSymlink(const QString& target, const QString& linkPath, QString* error = nullptr);
    bool compressItems(const QStringList& sources, const QString& destinationArchive,
                       int compressionLevel = -1, const QString& passphrase = QString());
    bool extractArchive(const QString& archivePath, const QString& destinationDir,
                        const QString& passphrase = QString());

    void cancel();
    void cancelAll();
    void pause();
    void resume();
    [[nodiscard]] bool isPaused() const;
    void skipCurrentFile();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int pendingJobCount() const;
    [[nodiscard]] int activeAndPendingJobCount() const;

signals:
    void operationStarted(OperationType type, int totalItems, qint64 totalBytes);
    void progress(const OperationProgress& progress);
    void conflictEncountered(const OperationConflict& conflict,
                             ConflictResolution& chosenResolution, bool& applyToAll);
    void operationFinished(const OperationResult& result);
    void queueStatusChanged(int currentJobIndex, int totalJobsInBatch);

private:
    enum class ItemOutcome { Done, Skipped, Failed };

    void executeCopyOrMove(OperationType type, const QStringList& sources, const QString& destDir,
                           ConflictResolution defaultRes);
    void executeDelete(const QStringList& sources);
    void executeTrash(const QStringList& sources);
    void executeDuplicate(const QStringList& sources);
    void executeCompress(const QStringList& sources, const QString& destinationArchive,
                         int compressionLevel, const QString& passphrase);
    void executeExtract(const QString& archivePath, const QString& destinationDir,
                        const QString& passphrase);

    ItemOutcome copySingleFile(const QString& src, const QString& dst,
                               ConflictResolution& appliedRes, bool& applyToAll);
    ItemOutcome copyDirectoryRecursive(const QString& src, const QString& dst,
                                       ConflictResolution& appliedRes, bool& applyToAll,
                                       bool mergeIntoExisting = false);
    ItemOutcome moveSingleItem(const QString& src, const QString& dst,
                               ConflictResolution& appliedRes, bool& applyToAll);

    QString generateDuplicateName(const QString& path);
    QString generateKeepBothName(const QString& destinationPath);

    ConflictResolution resolveFolderConflict(const QString& src, const QString& dst);
    void waitIfPaused();
    void updateProgressTiming();
    void resetProgressTiming();

    bool startWorker(const std::function<void()>& job);
    void startNextQueuedJob();
    void recordError(const QString& path, const QString& reason);
    void recordWarning(const QString& path, const QString& reason);
    void applyCollectedIssues(OperationResult& result);

    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_skipCurrentFile{false};
    ConflictResolution m_folderResolution{ConflictResolution::Ask};
    bool m_folderApplyToAll{false};

    QThread* m_workerThread{nullptr};
    QQueue<std::function<void()>> m_pendingJobs;
    int m_currentJobIndex{0};
    int m_totalJobsInBatch{0};

    OperationProgress m_currentProgress;
    qint64 m_progressStartMs{0};
    qint64 m_lastProgressEmitMs{0};
    qint64 m_lastTimeMs{0};
    qint64 m_lastBytes{0};

    QStringList m_itemErrors;
    QStringList m_itemWarnings;
    int m_skippedItems{0};
    bool m_failOnSkip{false};
};
