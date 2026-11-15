#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

enum class OperationType {
    Copy,
    Move,
    Duplicate,
    Delete,
    Trash,
    CreateFolder,
    CreateFile,
    Rename,
    CreateSymlink,
    Compress,
    Extract
};

enum class ConflictResolution { Ask, Merge, Replace, Skip, KeepBoth, Cancel };

enum class OperationItemStatus {
    Pending,
    Copying,
    Moving,
    Compressing,
    Extracting,
    Trashing,
    Deleting,
    Done,
    Failed,
    Skipped,
    Cancelled
};

struct OperationItem {
    quint64 id{0};
    QString sourcePath;
    QString displayName;
    qint64 totalBytes{0};
    qint64 processedBytes{0};
    OperationItemStatus status{OperationItemStatus::Pending};
    QString detail;
};

struct OperationConflict {
    QString sourcePath;
    QString destinationPath;
    qint64 sourceSize{0};
    qint64 destinationSize{0};
    QDateTime sourceModified;
    QDateTime destinationModified;
    bool sourceIsDir{false};
    bool destinationIsDir{false};

    [[nodiscard]] bool isFolderConflict() const {
        return sourceIsDir && destinationIsDir;
    }
};

struct OperationProgress {
    OperationType type;
    QString currentItem;
    qint64 bytesProcessed{0};
    qint64 totalBytes{0};
    int itemsProcessed{0};
    int totalItems{0};
    double speedBytesPerSec{0.0};
    qint64 elapsedMs{0};
    qint64 remainingMs{-1};
    int percent{0};
    int itemsCancelled{0};
    qint64 bytesCancelled{0};
};

struct OperationResult {
    OperationType type{OperationType::Copy};
    bool success{true};
    QString errorMessage;
    QStringList warnings;
    int processedCount{0};
    int skippedCount{0};
    int errorCount{0};
    int cancelledCount{0};
    QStringList createdPaths;
    QList<QPair<QString, QString>> movedPairs;   // (sourcePath, destinationPath)
    QList<QPair<QString, QString>> trashedPairs; // (sourcePath, trashFilePath)
};

Q_DECLARE_METATYPE(OperationItem)
Q_DECLARE_METATYPE(QList<OperationItem>)
