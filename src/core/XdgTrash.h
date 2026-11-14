#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <QFileInfo>

struct TrashItem {
    QString id;              // base name in Trash/files (e.g. "document.txt")
    QString originalPath;    // original absolute path
    QDateTime deletionDate;  // deletion timestamp
    QString trashFilePath;   // path in Trash/files/<id>
    QString trashInfoPath;   // path in Trash/info/<id>.trashinfo
    qint64 size{0};
    bool isDir{false};
};

class XdgTrash {
public:
    static void setCustomTrashDirectory(const QString& path);
    static QString trashDirectory();
    static QString trashFilesDirectory();
    static QString trashInfoDirectory();

    static bool moveToTrash(const QString& sourcePath, QString* errorMessage = nullptr, QString* trashedFilePath = nullptr);
    static bool restoreItem(const QString& trashFilePath, QString* errorMessage = nullptr,
                            QString* restoredPath = nullptr, bool keepBothOnConflict = true);
    static bool emptyTrash(QString* errorMessage = nullptr);
    static int purgeOlderThan(const QDateTime& cutoff, QString* errorMessage = nullptr);
    static QVector<TrashItem> listTrashItems();
    static qint64 totalTrashSize();

private:
    static bool ensureTrashDirs();
};
