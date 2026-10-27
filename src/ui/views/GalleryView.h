#pragma once

#include "FileSystemModel.h"
#include <QWidget>
#include <QListView>
#include <QLabel>
#include <QTextEdit>
#include <QSplitter>

class GalleryView : public QWidget {
    Q_OBJECT

public:
    explicit GalleryView(QWidget* parent = nullptr);

    void setFileSystemModel(FileSystemModel* model);
    [[nodiscard]] QString selectedPath() const { return m_selectedPath; }
    [[nodiscard]] QStringList selectedFilePaths() const { return m_selectedPath.isEmpty() ? QStringList{} : QStringList{m_selectedPath}; }

signals:
    void openPathRequested(const QString& path);
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onSelectionChanged();
    void onItemDoubleClicked(const QModelIndex& index);

private:
    void updateHeroPreview(const QString& path);

    FileSystemModel* m_model{nullptr};
    QString m_selectedPath;

    // Hero preview widgets
    QWidget* m_heroContainer{nullptr};
    QLabel* m_heroImageLabel{nullptr};
    QTextEdit* m_heroTextEdit{nullptr};
    QLabel* m_heroInfoLabel{nullptr};

    // Bottom filmstrip
    QListView* m_filmstrip{nullptr};
};
