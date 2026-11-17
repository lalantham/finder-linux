#include "FileKindClassifier.h"

namespace FileKind {
namespace {

// One table per category, deliberately identical to what the model and the
// search engine used before this was factored out.
const QSet<QString>& imageExtensions() {
    static const QSet<QString> extensions = {QStringLiteral("png"),  QStringLiteral("jpg"),
                                             QStringLiteral("jpeg"), QStringLiteral("gif"),
                                             QStringLiteral("svg"),  QStringLiteral("webp"),
                                             QStringLiteral("bmp"),  QStringLiteral("ico"),
                                             QStringLiteral("tiff"), QStringLiteral("tif")};
    return extensions;
}

const QSet<QString>& documentExtensions() {
    static const QSet<QString> extensions = {
        QStringLiteral("pdf"), QStringLiteral("doc"),  QStringLiteral("docx"),
        QStringLiteral("txt"), QStringLiteral("md"),   QStringLiteral("rtf"),
        QStringLiteral("odt"), QStringLiteral("xls"),  QStringLiteral("xlsx"),
        QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("csv")};
    return extensions;
}

const QSet<QString>& videoExtensions() {
    static const QSet<QString> extensions = {QStringLiteral("mp4"),  QStringLiteral("mkv"),
                                             QStringLiteral("avi"),  QStringLiteral("mov"),
                                             QStringLiteral("webm"), QStringLiteral("wmv"),
                                             QStringLiteral("flv"),  QStringLiteral("m4v")};
    return extensions;
}

const QSet<QString>& audioExtensions() {
    static const QSet<QString> extensions = {QStringLiteral("mp3"), QStringLiteral("flac"),
                                             QStringLiteral("wav"), QStringLiteral("ogg"),
                                             QStringLiteral("m4a"), QStringLiteral("aac"),
                                             QStringLiteral("wma"), QStringLiteral("opus")};
    return extensions;
}

const QSet<QString>& archiveExtensions() {
    static const QSet<QString> extensions = {
        QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"),  QStringLiteral("bz2"),
        QStringLiteral("xz"),  QStringLiteral("7z"),  QStringLiteral("rar"), QStringLiteral("tgz")};
    return extensions;
}

const QSet<QString>& emptyExtensions() {
    static const QSet<QString> empty;
    return empty;
}

} // namespace

Category fromFilterName(const QString& name) {
    const QString key = name.trimmed().toLower();
    if (key.isEmpty()) {
        return Category::None;
    }
    // Accept both singular and plural spellings, matching the search tokens
    // documented for kind: filters.
    if (key == QStringLiteral("folder") || key == QStringLiteral("folders")) {
        return Category::Folder;
    }
    if (key == QStringLiteral("image") || key == QStringLiteral("images")) {
        return Category::Image;
    }
    if (key == QStringLiteral("document") || key == QStringLiteral("documents")) {
        return Category::Document;
    }
    if (key == QStringLiteral("video") || key == QStringLiteral("videos")) {
        return Category::Video;
    }
    if (key == QStringLiteral("audio")) {
        return Category::Audio;
    }
    if (key == QStringLiteral("archive") || key == QStringLiteral("archives")) {
        return Category::Archive;
    }
    return Category::None;
}

const QSet<QString>& extensionsFor(Category category) {
    switch (category) {
    case Category::Image:
        return imageExtensions();
    case Category::Document:
        return documentExtensions();
    case Category::Video:
        return videoExtensions();
    case Category::Audio:
        return audioExtensions();
    case Category::Archive:
        return archiveExtensions();
    case Category::Folder:
    case Category::None:
        break;
    }
    return emptyExtensions();
}

bool matchesExtension(const QString& suffix, Category category) {
    const QSet<QString>& extensions = extensionsFor(category);
    if (extensions.isEmpty()) {
        return false;
    }
    QString clean = suffix;
    if (clean.startsWith(QLatin1Char('.'))) {
        clean.remove(0, 1);
    }
    return extensions.contains(clean.toLower());
}

} // namespace FileKind
