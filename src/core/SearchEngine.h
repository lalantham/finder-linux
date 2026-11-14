#pragma once

#include "FileItem.h"
#include <QObject>
#include <QThread>
#include <QVector>
#include <QMimeDatabase>
#include <atomic>

struct SearchFilter {
    QString query;
    QString searchPath;
    bool recursive{true};
    bool caseSensitive{false};
    bool includeHidden{false};
    QString kindFilter; // e.g., "image", "document", or empty for all
    qint64 minSize{-1};
    qint64 maxSize{-1};
};

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
    void searchDirectory(const QString& dirPath);
    bool matchesFilter(const QFileInfo& fi) const;

    SearchFilter m_filter;
    std::atomic<bool> m_cancelled{false};
    int m_count{0};
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

signals:
    void itemsFound(const QVector<FileItem>& items);
    void searchFinished(int totalFound);
    void searchProgress(const QString& currentDir);

private:
    QThread* m_workerThread{nullptr};
    SearchWorker* m_worker{nullptr};
};
