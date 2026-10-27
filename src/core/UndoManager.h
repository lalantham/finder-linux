#pragma once

#include <QString>
#include <QVector>
#include <QObject>
#include <functional>
#include <memory>

enum class UndoActionType {
    Create,
    Rename,
    Move,
    Trash
};

struct UndoAction {
    UndoActionType type;
    QString description;
    QString sourcePath;
    QString targetPath;
    QString trashInfoPath;
};

class UndoManager : public QObject {
    Q_OBJECT

public:
    explicit UndoManager(QObject* parent = nullptr);

    void pushAction(const UndoAction& action);
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString undoDescription() const;
    [[nodiscard]] QString redoDescription() const;

    bool undo(QString* errorMessage = nullptr);
    bool redo(QString* errorMessage = nullptr);
    void clear();

signals:
    void stackChanged();

private:
    QVector<UndoAction> m_undoStack;
    QVector<UndoAction> m_redoStack;
    static constexpr int MaxStackSize = 50;
};
