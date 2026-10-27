#pragma once

#include "AppSettings.h"
#include "FileSystemModel.h"
#include "IconView.h"
#include "ListView.h"
#include "ColumnView.h"
#include "GalleryView.h"
#include <QWidget>
#include <QStackedWidget>
#include <QVector>

class TabPage : public QWidget {
    Q_OBJECT

public:
    explicit TabPage(const QString& initialPath, QWidget* parent = nullptr);

    void navigateTo(const QString& path, bool recordHistory = true);
    void goBack();
    void goForward();
    void goUp();
    void refresh();

    [[nodiscard]] QString currentPath() const noexcept { return m_currentPath; }
    [[nodiscard]] bool canGoBack() const noexcept { return !m_backHistory.isEmpty(); }
    [[nodiscard]] bool canGoForward() const noexcept { return !m_forwardHistory.isEmpty(); }

    void setViewMode(ViewMode mode);
    [[nodiscard]] ViewMode viewMode() const noexcept { return m_viewMode; }

    void setShowHidden(bool show);
    void setFilterQuery(const QString& query);

    [[nodiscard]] FileSystemModel* model() const noexcept { return m_model; }
    [[nodiscard]] QString selectedFilePath() const;
    [[nodiscard]] QStringList selectedFilePaths() const;
    void selectAll();

signals:
    void pathChanged(const QString& newPath);
    void historyChanged();
    void quickLookRequested(const QString& path);
    void getInfoRequested(const QString& path);
    void statusMessageChanged(const QString& message);
    void contextMenuRequested(const QPoint& globalPos, const QStringList& selectedPaths, const QString& currentDir);
    void fileDropRequested(const QStringList& sources, const QString& targetDirectory, Qt::DropAction action);

private slots:
    void onDirectoryLoaded(const QString& path, int itemCount);

private:
    void updateStatus();

    QString m_currentPath;
    QVector<QString> m_backHistory;
    QVector<QString> m_forwardHistory;

    FileSystemModel* m_model{nullptr};
    ViewMode m_viewMode{ViewMode::Icon};

    QStackedWidget* m_viewStack{nullptr};
    IconView* m_iconView{nullptr};
    ListView* m_listView{nullptr};
    ColumnView* m_columnView{nullptr};
    GalleryView* m_galleryView{nullptr};
};
