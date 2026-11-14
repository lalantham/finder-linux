#include "RecentFilesManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>

namespace {

QString s_storagePathOverride;

struct ApplicationEntry {
    QString name;
    QString exec;
    QString modified;
    int count{1};
};

struct Bookmark {
    QString href;
    QString added;
    QString modified;
    QString visited;
    QString mimeType;
    QStringList groups;
    QVector<ApplicationEntry> applications;
};

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString toHref(const QString& path)
{
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

QString fromHref(const QString& href)
{
    const QUrl url(href);
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

QVector<Bookmark> readBookmarks(const QString& path, bool* ok)
{
    QVector<Bookmark> bookmarks;
    *ok = true;

    QFile file(path);
    if (!file.exists()) {
        return bookmarks;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *ok = false;
        return {};
    }
    if (file.size() == 0) {
        return bookmarks;
    }

    QXmlStreamReader xml(&file);
    Bookmark* current = nullptr;
    bool inApplications = false;
    bool inGroups = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("bookmark")) {
                Bookmark bookmark;
                bookmark.href = xml.attributes().value(QStringLiteral("href")).toString();
                bookmark.added = xml.attributes().value(QStringLiteral("added")).toString();
                bookmark.modified = xml.attributes().value(QStringLiteral("modified")).toString();
                bookmark.visited = xml.attributes().value(QStringLiteral("visited")).toString();
                bookmarks.append(bookmark);
                current = &bookmarks.last();
            } else if (current && name == QLatin1String("mime-type")) {
                current->mimeType = xml.attributes().value(QStringLiteral("type")).toString();
            } else if (current && name == QLatin1String("groups")) {
                inGroups = true;
            } else if (current && inGroups && name == QLatin1String("group")) {
                current->groups.append(xml.readElementText());
            } else if (current && name == QLatin1String("applications")) {
                inApplications = true;
            } else if (current && inApplications && name == QLatin1String("application")) {
                ApplicationEntry app;
                app.name = xml.attributes().value(QStringLiteral("name")).toString();
                app.exec = xml.attributes().value(QStringLiteral("exec")).toString();
                app.modified = xml.attributes().value(QStringLiteral("modified")).toString();
                app.count = xml.attributes().value(QStringLiteral("count")).toInt();
                if (app.count < 1) {
                    app.count = 1;
                }
                current->applications.append(app);
            }
        } else if (xml.isEndElement()) {
            const QStringView name = xml.name();
            if (name == QLatin1String("bookmark")) {
                current = nullptr;
            } else if (name == QLatin1String("applications")) {
                inApplications = false;
            } else if (name == QLatin1String("groups")) {
                inGroups = false;
            }
        }
    }

    if (xml.hasError()) {
        *ok = false;
        return {};
    }
    return bookmarks;
}

bool writeBookmarks(const QString& path, const QVector<Bookmark>& bookmarks)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("xbel"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeAttribute(QStringLiteral("xmlns:bookmark"),
                       QStringLiteral("http://www.freedesktop.org/standards/desktop-bookmarks"));
    xml.writeAttribute(QStringLiteral("xmlns:mime"),
                       QStringLiteral("http://www.freedesktop.org/standards/shared-mime-info"));

    for (const Bookmark& bookmark : bookmarks) {
        if (bookmark.href.isEmpty()) {
            continue;
        }

        xml.writeStartElement(QStringLiteral("bookmark"));
        xml.writeAttribute(QStringLiteral("href"), bookmark.href);
        if (!bookmark.added.isEmpty()) {
            xml.writeAttribute(QStringLiteral("added"), bookmark.added);
        }
        if (!bookmark.modified.isEmpty()) {
            xml.writeAttribute(QStringLiteral("modified"), bookmark.modified);
        }
        if (!bookmark.visited.isEmpty()) {
            xml.writeAttribute(QStringLiteral("visited"), bookmark.visited);
        }

        xml.writeStartElement(QStringLiteral("info"));
        xml.writeStartElement(QStringLiteral("metadata"));
        xml.writeAttribute(QStringLiteral("owner"), QStringLiteral("http://freedesktop.org"));

        if (!bookmark.mimeType.isEmpty()) {
            xml.writeStartElement(QStringLiteral("mime:mime-type"));
            xml.writeAttribute(QStringLiteral("type"), bookmark.mimeType);
            xml.writeEndElement();
        }

        if (!bookmark.groups.isEmpty()) {
            xml.writeStartElement(QStringLiteral("bookmark:groups"));
            for (const QString& group : bookmark.groups) {
                xml.writeTextElement(QStringLiteral("bookmark:group"), group);
            }
            xml.writeEndElement();
        }

        if (!bookmark.applications.isEmpty()) {
            xml.writeStartElement(QStringLiteral("bookmark:applications"));
            for (const ApplicationEntry& app : bookmark.applications) {
                xml.writeStartElement(QStringLiteral("bookmark:application"));
                xml.writeAttribute(QStringLiteral("name"), app.name);
                if (!app.exec.isEmpty()) {
                    xml.writeAttribute(QStringLiteral("exec"), app.exec);
                }
                if (!app.modified.isEmpty()) {
                    xml.writeAttribute(QStringLiteral("modified"), app.modified);
                }
                xml.writeAttribute(QStringLiteral("count"), QString::number(app.count));
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }

        xml.writeEndElement(); // metadata
        xml.writeEndElement(); // info
        xml.writeEndElement(); // bookmark
    }

    xml.writeEndElement(); // xbel
    xml.writeEndDocument();
    return file.commit();
}

} // namespace

QString RecentFilesManager::storagePath()
{
    if (!s_storagePathOverride.isEmpty()) {
        return s_storagePathOverride;
    }

    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataDirectory.isEmpty()) {
        return {};
    }
    return dataDirectory + QStringLiteral("/recently-used.xbel");
}

void RecentFilesManager::setStoragePathForTesting(const QString& path)
{
    s_storagePathOverride = path;
}

bool RecentFilesManager::add(const QString& path, const QString& mimeType, const QString& appName)
{
    if (path.isEmpty()) {
        return false;
    }

    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return false;
    }

    const QString absolutePath = info.absoluteFilePath();
    const QString href = toHref(absolutePath);
    if (href.isEmpty()) {
        return false;
    }

    QString resolvedMime = mimeType;
    if (resolvedMime.isEmpty()) {
        QMimeDatabase database;
        resolvedMime = database.mimeTypeForFile(absolutePath, QMimeDatabase::MatchDefault).name();
    }

    const QString storage = storagePath();
    if (storage.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(storage).absolutePath());

    bool ok = false;
    QVector<Bookmark> bookmarks = readBookmarks(storage, &ok);
    if (!ok) {
        return false;
    }

    const QString now = nowIso();
    const QString entryName = appName.isEmpty() ? QStringLiteral("Finder") : appName;
    const QString entryExec = QStringLiteral("finder-linux %u");

    int index = -1;
    for (int i = 0; i < bookmarks.size(); ++i) {
        if (bookmarks.at(i).href == href || fromHref(bookmarks.at(i).href) == absolutePath) {
            index = i;
            break;
        }
    }

    Bookmark bookmark;
    if (index >= 0) {
        bookmark = bookmarks.takeAt(index);
        if (bookmark.added.isEmpty()) {
            bookmark.added = now;
        }
    } else {
        bookmark.added = now;
    }

    bookmark.href = href;
    bookmark.modified = now;
    bookmark.visited = now;
    if (!resolvedMime.isEmpty()) {
        bookmark.mimeType = resolvedMime;
    }

    bool applicationFound = false;
    for (ApplicationEntry& app : bookmark.applications) {
        if (app.name == entryName) {
            app.exec = entryExec;
            app.modified = now;
            app.count += 1;
            applicationFound = true;
            break;
        }
    }
    if (!applicationFound) {
        ApplicationEntry app;
        app.name = entryName;
        app.exec = entryExec;
        app.modified = now;
        app.count = 1;
        bookmark.applications.append(app);
    }

    bookmarks.prepend(bookmark);
    return writeBookmarks(storage, bookmarks);
}

QList<RecentFile> RecentFilesManager::list(int limit)
{
    QList<RecentFile> result;
    if (limit <= 0) {
        return result;
    }

    const QString storage = storagePath();
    if (storage.isEmpty()) {
        return result;
    }

    bool ok = false;
    const QVector<Bookmark> bookmarks = readBookmarks(storage, &ok);
    if (!ok) {
        return result;
    }

    QSet<QString> seen;
    for (const Bookmark& bookmark : bookmarks) {
        const QString path = fromHref(bookmark.href);
        if (path.isEmpty() || seen.contains(path)) {
            continue;
        }

        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) {
            continue;
        }

        seen.insert(path);
        RecentFile recent;
        recent.path = path;
        recent.mimeType = bookmark.mimeType;
        recent.modified = QDateTime::fromString(bookmark.modified, Qt::ISODate);
        if (!recent.modified.isValid()) {
            recent.modified = QDateTime::fromString(bookmark.visited, Qt::ISODate);
        }
        result.append(recent);
    }

    std::stable_sort(result.begin(), result.end(), [](const RecentFile& a, const RecentFile& b) {
        return a.modified > b.modified;
    });

    if (result.size() > limit) {
        result = result.mid(0, limit);
    }
    return result;
}

void RecentFilesManager::clear()
{
    const QString storage = storagePath();
    if (storage.isEmpty()) {
        return;
    }
    QFile::remove(storage);
}
