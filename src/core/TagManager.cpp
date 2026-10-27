#include "TagManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QPixmap>
#include <sys/xattr.h>
#include <cstring>

static const QList<TagInfo> s_standardTags = {
    {QStringLiteral("Red"),    QColor(0xff, 0x3b, 0x30)},
    {QStringLiteral("Orange"), QColor(0xff, 0x95, 0x00)},
    {QStringLiteral("Yellow"), QColor(0xff, 0xcc, 0x00)},
    {QStringLiteral("Green"),  QColor(0x34, 0xc7, 0x59)},
    {QStringLiteral("Blue"),   QColor(0x00, 0x7a, 0xff)},
    {QStringLiteral("Purple"), QColor(0xaf, 0x52, 0xde)},
    {QStringLiteral("Gray"),   QColor(0x8e, 0x8e, 0x93)}
};

TagManager& TagManager::instance()
{
    static TagManager inst;
    return inst;
}

TagManager::TagManager()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/FinderLinux");
    QDir().mkpath(configDir);
    m_registryPath = configDir + QStringLiteral("/tags.json");

    loadRegistry();
}

TagManager::~TagManager()
{
    saveRegistry();
}

const QList<TagInfo>& TagManager::standardTags()
{
    return s_standardTags;
}

QColor TagManager::colorForTag(const QString& tag)
{
    for (const auto& t : s_standardTags) {
        if (t.name.compare(tag, Qt::CaseInsensitive) == 0) {
            return t.color;
        }
    }
    // Fallback based on name hash if custom tag
    int hue = static_cast<int>(qHash(tag) % 360);
    return QColor::fromHsv(hue, 180, 230);
}

QIcon TagManager::tagDotIcon(const QString& tag, int size)
{
    return tagDotIcon(colorForTag(tag), size);
}

QIcon TagManager::tagDotIcon(const QColor& color, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Outer subtle border
    painter.setPen(color.darker(115));
    painter.setBrush(color);
    painter.drawEllipse(1, 1, size - 2, size - 2);

    return QIcon(pix);
}

QStringList TagManager::readXattr(const QString& path)
{
    QByteArray localPath = QFile::encodeName(path);
    ssize_t size = getxattr(localPath.constData(), "user.xdg.tags", nullptr, 0);
    if (size <= 0) return {};

    QByteArray buf(static_cast<int>(size), '\0');
    ssize_t readBytes = getxattr(localPath.constData(), "user.xdg.tags", buf.data(), static_cast<size_t>(size));
    if (readBytes <= 0) return {};

    QString raw = QString::fromUtf8(buf.constData(), static_cast<int>(readBytes)).trimmed();
    if (raw.isEmpty()) return {};

    QStringList list;
    if (raw.contains(QLatin1Char('\n'))) {
        list = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    } else {
        list = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    }

    QStringList cleanList;
    for (const auto& item : list) {
        QString trimmed = item.trimmed();
        if (!trimmed.isEmpty() && !cleanList.contains(trimmed)) {
            cleanList.append(trimmed);
        }
    }
    return cleanList;
}

bool TagManager::writeXattr(const QString& path, const QStringList& tags)
{
    QByteArray localPath = QFile::encodeName(path);
    if (tags.isEmpty()) {
        removexattr(localPath.constData(), "user.xdg.tags");
        return true;
    }

    QByteArray tagData = tags.join(QLatin1Char(',')).toUtf8();
    int ret = setxattr(localPath.constData(), "user.xdg.tags", tagData.constData(), static_cast<size_t>(tagData.size()), 0);
    return (ret == 0);
}

void TagManager::loadRegistry()
{
    QFile f(m_registryPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QString path = it.key();
        QJsonArray arr = it.value().toArray();
        QStringList tags;
        for (const auto& v : arr) {
            tags.append(v.toString());
        }
        if (!tags.isEmpty() && QFileInfo::exists(path)) {
            m_fileTags.insert(path, tags);
            for (const auto& t : tags) {
                m_tagFiles[t].insert(path);
            }
        }
    }
}

void TagManager::saveRegistry()
{
    QJsonObject root;
    for (auto it = m_fileTags.begin(); it != m_fileTags.end(); ++it) {
        if (QFileInfo::exists(it.key()) && !it.value().isEmpty()) {
            QJsonArray arr;
            for (const auto& t : it.value()) {
                arr.append(t);
            }
            root.insert(it.key(), arr);
        }
    }

    QFile f(m_registryPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

QStringList TagManager::tagsForFile(const QString& path)
{
    QString clean = QDir::cleanPath(path);
    QMutexLocker locker(&m_mutex);

    // 1. Try xattr first for native interop
    QStringList xattrTags = readXattr(clean);
    if (!xattrTags.isEmpty()) {
        // Sync into memory cache
        m_fileTags.insert(clean, xattrTags);
        for (const auto& t : xattrTags) {
            m_tagFiles[t].insert(clean);
        }
        return xattrTags;
    }

    // 2. Fall back to internal persistent registry
    return m_fileTags.value(clean);
}

bool TagManager::hasTag(const QString& path, const QString& tag)
{
    return tagsForFile(path).contains(tag, Qt::CaseInsensitive);
}

bool TagManager::setTagsForFile(const QString& path, const QStringList& tags)
{
    QString clean = QDir::cleanPath(path);
    if (!QFileInfo::exists(clean)) return false;

    {
        QMutexLocker locker(&m_mutex);

        // Remove previous mappings from m_tagFiles
        QStringList oldTags = m_fileTags.value(clean);
        for (const auto& ot : oldTags) {
            m_tagFiles[ot].remove(clean);
        }

        if (tags.isEmpty()) {
            m_fileTags.remove(clean);
        } else {
            m_fileTags.insert(clean, tags);
            for (const auto& nt : tags) {
                m_tagFiles[nt].insert(clean);
            }
        }

        // Write xattr (best effort, ignore failure on non-supported fs)
        writeXattr(clean, tags);
        saveRegistry();
    }

    emit tagsChanged(clean, tags);
    return true;
}

bool TagManager::addTag(const QString& path, const QString& tag)
{
    QStringList curr = tagsForFile(path);
    for (const auto& t : curr) {
        if (t.compare(tag, Qt::CaseInsensitive) == 0) return true; // Already has it
    }
    curr.append(tag);
    return setTagsForFile(path, curr);
}

bool TagManager::removeTag(const QString& path, const QString& tag)
{
    QStringList curr = tagsForFile(path);
    QStringList updated;
    for (const auto& t : curr) {
        if (t.compare(tag, Qt::CaseInsensitive) != 0) {
            updated.append(t);
        }
    }
    return setTagsForFile(path, updated);
}

bool TagManager::toggleTag(const QString& path, const QString& tag)
{
    if (hasTag(path, tag)) {
        return removeTag(path, tag);
    } else {
        return addTag(path, tag);
    }
}

bool TagManager::clearTags(const QString& path)
{
    return setTagsForFile(path, {});
}

QStringList TagManager::filesForTag(const QString& tag)
{
    QMutexLocker locker(&m_mutex);
    QStringList result;
    for (const auto& path : m_tagFiles.value(tag)) {
        if (QFileInfo::exists(path)) {
            result.append(path);
        }
    }
    return result;
}

int TagManager::tagCount(const QString& tag)
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_tagFiles.value(tag).size());
}
