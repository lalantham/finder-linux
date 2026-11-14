#include "UndoManager.h"
#include "XdgTrash.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <sys/stat.h>

namespace {

bool copyDirectoryHelper(const QString& src, const QString& dst)
{
    QDir sDir(src);
    if (!QDir().mkpath(dst)) return false;
    for (const auto& e : sDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System)) {
        QString sItem = e.absoluteFilePath();
        QString dItem = dst + QStringLiteral("/") + e.fileName();
        if (e.isSymLink()) {
            if (!QFile::link(e.symLinkTarget(), dItem)) return false;
        } else if (e.isDir()) {
            if (!copyDirectoryHelper(sItem, dItem)) return false;
        } else {
            struct stat st {};
            if (::lstat(QFile::encodeName(sItem).constData(), &st) != 0 || !S_ISREG(st.st_mode)) {
                return false; // never fake-copy FIFOs, sockets or device nodes
            }
            if (!QFile::copy(sItem, dItem)) return false;
        }
    }
    return true;
}

// Confirms that a fallback copy is complete before the source may be removed.
bool treeMatches(const QString& src, const QString& dst)
{
    QFileInfo sInfo(src);
    QFileInfo dInfo(dst);

    if (sInfo.isSymLink()) {
        return dInfo.isSymLink() && dInfo.symLinkTarget() == sInfo.symLinkTarget();
    }
    if (sInfo.isDir()) {
        if (!dInfo.isDir()) return false;
        const auto entries = QDir(src).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
        for (const auto& e : entries) {
            if (!treeMatches(e.absoluteFilePath(), dst + QStringLiteral("/") + e.fileName())) {
                return false;
            }
        }
        return true;
    }
    if (!dInfo.exists() || dInfo.isDir()) return false;
    return dInfo.size() == sInfo.size();
}

bool movePathSafe(const QString& src, const QString& dst)
{
    // POSIX rename replaces an existing file atomically, but QFile::rename
    // refuses to overwrite. The callers validate that dst is free.
    if (QFile::rename(src, dst)) return true;

    QFileInfo srcFi(src);
    if (srcFi.isSymLink()) {
        if (QFile::link(srcFi.symLinkTarget(), dst) && treeMatches(src, dst)) {
            QFile::remove(src);
            return true;
        }
        QFile::remove(dst);
        return false;
    }

    if (srcFi.isDir()) {
        if (copyDirectoryHelper(src, dst) && treeMatches(src, dst)) {
            QDir(src).removeRecursively();
            return true;
        }
        QDir(dst).removeRecursively();
        return false;
    }

    if (QFile::copy(src, dst) && treeMatches(src, dst)) {
        QFile::remove(src);
        return true;
    }
    QFile::remove(dst);
    return false;
}

} // namespace

UndoManager::UndoManager(QObject* parent)
    : QObject(parent)
{
}

void UndoManager::pushAction(const UndoAction& action)
{
    m_undoStack.append(action);
    if (m_undoStack.size() > MaxStackSize) {
        m_undoStack.removeFirst();
    }
    m_redoStack.clear();
    emit stackChanged();
}

bool UndoManager::canUndo() const
{
    return !m_undoStack.isEmpty();
}

bool UndoManager::canRedo() const
{
    return !m_redoStack.isEmpty();
}

QString UndoManager::undoDescription() const
{
    if (m_undoStack.isEmpty()) return {};
    return m_undoStack.last().description;
}

QString UndoManager::redoDescription() const
{
    if (m_redoStack.isEmpty()) return {};
    return m_redoStack.last().description;
}

bool UndoManager::undo(QString* errorMessage)
{
    if (m_undoStack.isEmpty()) return false;
    UndoAction action = m_undoStack.takeLast();
    bool success = false;

    switch (action.type) {
    case UndoActionType::Create: {
        // Undoing a creation must never delete content the user added later.
        QFileInfo fi(action.targetPath);
        if (!fi.exists() && !fi.isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Item no longer exists: %1").arg(action.targetPath);
            break;
        }
        if (fi.isDir() && !fi.isSymLink()) {
            QDir dir(action.targetPath);
            if (!dir.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("\"%1\" is no longer empty; refusing to delete it.")
                                        .arg(fi.fileName());
                }
                break;
            }
            success = QDir().rmdir(action.targetPath);
        } else {
            if (action.createdSize >= 0 &&
                (fi.size() != action.createdSize || fi.lastModified() != action.createdModified)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("\"%1\" was modified after it was created; refusing to delete it.")
                                        .arg(fi.fileName());
                }
                break;
            }
            success = QFile::remove(action.targetPath);
        }
        if (!success && errorMessage) {
            *errorMessage = QStringLiteral("Failed to remove created item: %1").arg(action.targetPath);
        }
        break;
    }
    case UndoActionType::Rename: {
        // targetPath was renamed from sourcePath -> rename targetPath back to sourcePath
        if (!QFile::exists(action.targetPath) && !QFileInfo(action.targetPath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo rename; item not found: %1").arg(action.targetPath);
            break;
        }
        if (QFile::exists(action.sourcePath) || QFileInfo(action.sourcePath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo rename; original path already exists: %1").arg(action.sourcePath);
            break;
        }
        success = QFile::rename(action.targetPath, action.sourcePath);
        if (!success && errorMessage) {
            *errorMessage = QStringLiteral("Failed to rename %1 back to %2").arg(action.targetPath, action.sourcePath);
        }
        break;
    }
    case UndoActionType::Move: {
        // targetPath was moved from sourcePath -> move back
        QFileInfo targetFi(action.targetPath);
        if (!targetFi.exists() && !targetFi.isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo move; item not found: %1").arg(action.targetPath);
            break;
        }
        if (QFile::exists(action.sourcePath) || QFileInfo(action.sourcePath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo move; destination already exists: %1").arg(action.sourcePath);
            break;
        }
        success = movePathSafe(action.targetPath, action.sourcePath);
        if (!success && errorMessage) {
            *errorMessage = QStringLiteral("Failed to move %1 back to %2").arg(action.targetPath, action.sourcePath);
        }
        break;
    }
    case UndoActionType::Trash: {
        // sourcePath was moved to trash (trashInfoPath / targetPath) -> restore
        success = XdgTrash::restoreItem(action.targetPath, errorMessage);
        break;
    }
    }

    if (success) {
        m_redoStack.append(action);
    }
    emit stackChanged();
    return success;
}

bool UndoManager::redo(QString* errorMessage)
{
    if (m_redoStack.isEmpty()) return false;
    UndoAction action = m_redoStack.takeLast();
    bool success = false;

    switch (action.type) {
    case UndoActionType::Create: {
        if (QFile::exists(action.targetPath) || QFileInfo(action.targetPath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("\"%1\" already exists again; nothing was re-created.")
                                                  .arg(QFileInfo(action.targetPath).fileName());
            break;
        }
        if (action.isDirectory) {
            success = QDir().mkpath(action.targetPath) && QDir(action.targetPath).exists();
        } else {
            QFile f(action.targetPath);
            // NewOnly: never truncate a file that appeared in the meantime.
            success = f.open(QIODevice::WriteOnly | QIODevice::NewOnly);
            if (success) f.close();
        }
        if (success) {
            QFileInfo created(action.targetPath);
            action.isDirectory = created.isDir();
            action.createdSize = created.isDir() ? 0 : created.size();
            action.createdModified = created.lastModified();
        } else if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to re-create %1").arg(action.targetPath);
        }
        break;
    }
    case UndoActionType::Rename: {
        if (!QFile::exists(action.sourcePath) && !QFileInfo(action.sourcePath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot redo rename; item not found: %1").arg(action.sourcePath);
            break;
        }
        if (QFile::exists(action.targetPath) || QFileInfo(action.targetPath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot redo rename; destination already exists: %1").arg(action.targetPath);
            break;
        }
        success = QFile::rename(action.sourcePath, action.targetPath);
        if (!success && errorMessage) {
            *errorMessage = QStringLiteral("Failed to rename %1 to %2").arg(action.sourcePath, action.targetPath);
        }
        break;
    }
    case UndoActionType::Move: {
        if (!QFile::exists(action.sourcePath) && !QFileInfo(action.sourcePath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot redo move; item not found: %1").arg(action.sourcePath);
            break;
        }
        if (QFile::exists(action.targetPath) || QFileInfo(action.targetPath).isSymLink()) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot redo move; destination already exists: %1").arg(action.targetPath);
            break;
        }
        success = movePathSafe(action.sourcePath, action.targetPath);
        if (!success && errorMessage) {
            *errorMessage = QStringLiteral("Failed to move %1 to %2").arg(action.sourcePath, action.targetPath);
        }
        break;
    }
    case UndoActionType::Trash: {
        success = XdgTrash::moveToTrash(action.sourcePath, errorMessage);
        break;
    }
    }

    if (success) {
        m_undoStack.append(action);
    }
    emit stackChanged();
    return success;
}

void UndoManager::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
    emit stackChanged();
}
