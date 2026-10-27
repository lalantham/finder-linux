#include "UndoManager.h"
#include "XdgTrash.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>

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
        // Created file or dir: undo by deleting it
        QFileInfo fi(action.targetPath);
        if (!fi.exists()) {
            if (errorMessage) *errorMessage = QStringLiteral("Item no longer exists: %1").arg(action.targetPath);
            break;
        }
        if (fi.isDir()) {
            success = QDir(action.targetPath).removeRecursively();
        } else {
            success = QFile::remove(action.targetPath);
        }
        if (!success && errorMessage) {
            *errorMessage = QStringLiteral("Failed to remove created item: %1").arg(action.targetPath);
        }
        break;
    }
    case UndoActionType::Rename: {
        // targetPath was renamed from sourcePath -> rename targetPath back to sourcePath
        if (!QFile::exists(action.targetPath)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo rename; item not found: %1").arg(action.targetPath);
            break;
        }
        if (QFile::exists(action.sourcePath)) {
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
        if (!QFile::exists(action.targetPath)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo move; item not found: %1").arg(action.targetPath);
            break;
        }
        if (QFile::exists(action.sourcePath)) {
            if (errorMessage) *errorMessage = QStringLiteral("Cannot undo move; destination already exists: %1").arg(action.sourcePath);
            break;
        }
        success = QFile::rename(action.targetPath, action.sourcePath);
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
        QFileInfo fi(action.targetPath);
        if (fi.isDir()) {
            success = QDir().mkpath(action.targetPath);
        } else {
            QFile f(action.targetPath);
            success = f.open(QIODevice::WriteOnly);
            if (success) f.close();
        }
        break;
    }
    case UndoActionType::Rename:
    case UndoActionType::Move: {
        success = QFile::rename(action.sourcePath, action.targetPath);
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
