#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>

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

enum class ConflictResolution {
    Ask,
    Replace,
    Skip,
    KeepBoth,
    Cancel
};

struct OperationConflict {
    QString sourcePath;
    QString destinationPath;
    qint64 sourceSize{0};
    qint64 destinationSize{0};
    QDateTime sourceModified;
    QDateTime destinationModified;
};

struct OperationProgress {
    OperationType type;
    QString currentItem;
    qint64 bytesProcessed{0};
    qint64 totalBytes{0};
    int itemsProcessed{0};
    int totalItems{0};
    double speedBytesPerSec{0.0};
    int percent{0};
};

struct OperationResult {
    OperationType type{OperationType::Copy};
    bool success{true};
    QString errorMessage;
    QStringList warnings;
    int processedCount{0};
    int skippedCount{0};
    int errorCount{0};
    QStringList createdPaths;
    QList<QPair<QString, QString>> movedPairs;   // (sourcePath, destinationPath)
    QList<QPair<QString, QString>> trashedPairs; // (sourcePath, trashFilePath)
};
