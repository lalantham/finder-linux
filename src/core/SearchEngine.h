#pragma once

#include "FileItem.h"
#include "SearchQuery.h"
#include <QMimeDatabase>
#include <QObject>
#include <QThread>
#include <QVector>
#include <atomic>

class SearchWorker : public QObject {
    Q_OBJECT

public:
    explicit SearchWorker(SearchFilter filter);
    void cancel();

public slots:
    void run();

signals:
    void itemsFound(const QVector<FileItem>& items);
    void finished(int totalFound);
    void progress(const QString& currentDir);

private:
    void searchDirectory(const QString& dirPath, const QString& rootPath);
    bool matchesFilter(const QFileInfo& fi) const;

    SearchFilter m_filter;
    std::atomic<bool> m_cancelled{false};
    int m_count{0};
    int m_progressCounter{0};
    mutable QMimeDatabase m_mimeDb;
};

class SearchEngine : public QObject {
    Q_OBJECT

public:
    explicit SearchEngine(QObject* parent = nullptr);
    ~SearchEngine() override;

    void startSearch(const SearchFilter& filter);
    void stopSearch();
    [[nodiscard]] bool isRunning() const;

    // Local filesystem roots used by the "This PC" scope.
    [[nodiscard]] static QStringList localSearchRoots();

signals:
    void itemsFound(const QVector<FileItem>& items);
    void searchFinished(int totalFound);
    void searchProgress(const QString& currentDir);

private:
    QThread* m_workerThread{nullptr};
    SearchWorker* m_worker{nullptr};
};
