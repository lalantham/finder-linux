#pragma once

#include "FileOperation.h"
#include <QObject>
#include <QThread>
#include <QStringList>
#include <atomic>
#include <functional>

class FileOperationEngine : public QObject {
    Q_OBJECT

public:
    explicit FileOperationEngine(QObject* parent = nullptr);
    ~FileOperationEngine() override;

    void copyItems(const QStringList& sources, const QString& destinationDir, ConflictResolution res = ConflictResolution::Ask);
    void moveItems(const QStringList& sources, const QString& destinationDir, ConflictResolution res = ConflictResolution::Ask);
    void duplicateItems(const QStringList& sources);
    void trashItems(const QStringList& sources);
    void deletePermanently(const QStringList& sources);
    bool createFolder(const QString& parentDir, const QString& folderName, QString* createdPath = nullptr, QString* error = nullptr);
    bool createFile(const QString& parentDir, const QString& fileName, QString* createdPath = nullptr, QString* error = nullptr);
    bool renameItem(const QString& sourcePath, const QString& newName, QString* renamedPath = nullptr, QString* error = nullptr);
    bool createSymlink(const QString& target, const QString& linkPath, QString* error = nullptr);

    void cancel();
    [[nodiscard]] bool isRunning() const;

signals:
    void operationStarted(OperationType type, int totalItems, qint64 totalBytes);
    void progress(const OperationProgress& progress);
    void conflictEncountered(const OperationConflict& conflict, ConflictResolution& chosenResolution, bool& applyToAll);
    void operationFinished(const OperationResult& result);

private:
    void runAsyncOperation(OperationType type, const QStringList& sources, const QString& destDir, ConflictResolution defaultRes);
    void executeCopyOrMove(OperationType type, const QStringList& sources, const QString& destDir, ConflictResolution defaultRes);
    void executeDelete(const QStringList& sources);
    void executeTrash(const QStringList& sources);
    void executeDuplicate(const QStringList& sources);

    bool copySingleFile(const QString& src, const QString& dst, ConflictResolution& appliedRes, bool& applyToAll);
    bool copyDirectoryRecursive(const QString& src, const QString& dst, ConflictResolution& appliedRes, bool& applyToAll);
    bool moveSingleItem(const QString& src, const QString& dst, ConflictResolution& appliedRes, bool& applyToAll);

    QString generateDuplicateName(const QString& path);
    QString generateKeepBothName(const QString& destinationPath);

    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_running{false};
    QThread* m_workerThread{nullptr};

    OperationProgress m_currentProgress;
    qint64 m_lastTimeMs{0};
    qint64 m_lastBytes{0};
};
