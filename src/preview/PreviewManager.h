#pragma once

#include "FileItem.h"
#include <QObject>
#include <QPixmap>
#include <QCache>
#include <QSize>

class PreviewManager : public QObject {
    Q_OBJECT

public:
    static PreviewManager& instance();

    QPixmap getThumbnail(const QString& path, const QSize& targetSize = QSize(128, 128));
    QString getTextPreview(const QString& path, int maxBytes = 64 * 1024);
    QPixmap getPdfPreview(const QString& path, const QSize& targetSize = QSize(600, 800));

    [[nodiscard]] bool isImage(const QString& path) const;
    [[nodiscard]] bool isText(const QString& path) const;
    [[nodiscard]] bool isPdf(const QString& path) const;

private:
    PreviewManager();
    ~PreviewManager() override = default;

    QCache<QString, QPixmap> m_thumbnailCache;
};
