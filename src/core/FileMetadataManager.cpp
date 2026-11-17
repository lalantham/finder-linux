#include "FileMetadataManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

namespace {
QString s_storagePathOverride;

QString defaultStoragePath() {
    if (!s_storagePathOverride.isEmpty())
        return s_storagePathOverride;
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/file-manager/metadata.json");
}
} // namespace

FileMetadataManager& FileMetadataManager::instance() {
    static FileMetadataManager manager;
    return manager;
}

void FileMetadataManager::setStoragePathForTesting(const QString& path) {
    s_storagePathOverride = path;
    auto& manager = instance();
    manager.m_storagePath = defaultStoragePath();
    manager.m_added.clear();
    manager.m_opened.clear();
    manager.load();
}

FileMetadataManager::FileMetadataManager() : m_storagePath(defaultStoragePath()) {
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &FileMetadataManager::save);
    load();
}

FileMetadataManager::~FileMetadataManager() {
    save();
}

void FileMetadataManager::load() {
    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();

    const auto readMap = [](const QJsonObject& object) {
        QHash<QString, qint64> result;
        for (auto it = object.begin(); it != object.end(); ++it) {
            result.insert(it.key(), static_cast<qint64>(it.value().toDouble()));
        }
        return result;
    };

    m_added = readMap(root.value(QStringLiteral("added")).toObject());
    m_opened = readMap(root.value(QStringLiteral("opened")).toObject());
}

void FileMetadataManager::save() {
    if (m_saveTimer)
        m_saveTimer->stop();
    if (!QDir().mkpath(QFileInfo(m_storagePath).absolutePath()))
        return;

    // QJsonObject keeps its keys sorted, so inserting in hash order makes every
    // insert an O(n) memmove: with a large store that alone costs hundreds of
    // milliseconds. Building the object from sorted keys turns the whole save
    // into a linear pass.
    QJsonObject added = buildSortedObject(m_added);
    QJsonObject opened = buildSortedObject(m_opened);

    QJsonObject root;
    root.insert(QStringLiteral("added"), added);
    root.insert(QStringLiteral("opened"), opened);

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

QJsonObject FileMetadataManager::buildSortedObject(const QHash<QString, qint64>& source) {
    QStringList keys = source.keys();
    keys.sort();
    QJsonObject object;
    for (const QString& key : keys) {
        object.insert(key, static_cast<double>(source.value(key)));
    }
    return object;
}

int FileMetadataManager::pruneMissingEntries(int maxEntries) {
    // Stat every path and drop the ones that no longer exist. This is the only
    // way the store can shrink: browsing folders registers items forever
    // otherwise, and the file grows without bound.
    const QStringList addedPaths = m_added.keys();
    int removed = 0;
    for (const QString& path : addedPaths) {
        if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) {
            m_added.remove(path);
            m_opened.remove(path);
            ++removed;
        }
    }

    // An explicit cap as a backstop: keep the most recently added entries.
    if (maxEntries > 0 && m_added.size() > maxEntries) {
        QVector<QPair<qint64, QString>> byAge;
        byAge.reserve(m_added.size());
        for (auto it = m_added.cbegin(); it != m_added.cend(); ++it) {
            byAge.append({it.value(), it.key()});
        }
        std::sort(byAge.begin(), byAge.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        const int excess = static_cast<int>(m_added.size()) - maxEntries;
        for (int i = 0; i < excess; ++i) {
            m_added.remove(byAge.at(i).second);
            m_opened.remove(byAge.at(i).second);
            ++removed;
        }
    }

    if (removed > 0) {
        scheduleSave();
    }
    return removed;
}

void FileMetadataManager::scheduleSave() {
    if (m_saveTimer)
        m_saveTimer->start();
}

QDateTime FileMetadataManager::dateAdded(const QString& path) const {
    const auto it = m_added.constFind(QDir::cleanPath(path));
    if (it == m_added.cend())
        return {};
    return QDateTime::fromMSecsSinceEpoch(it.value());
}

QDateTime FileMetadataManager::lastOpened(const QString& path) const {
    const auto it = m_opened.constFind(QDir::cleanPath(path));
    if (it == m_opened.cend())
        return {};
    return QDateTime::fromMSecsSinceEpoch(it.value());
}

void FileMetadataManager::registerPaths(const QStringList& paths) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (const QString& path : paths) {
        if (path.isEmpty())
            continue;
        const QString clean = QDir::cleanPath(path);
        if (!m_added.contains(clean)) {
            m_added.insert(clean, now);
            changed = true;
        }
    }
    if (changed)
        scheduleSave();
}

void FileMetadataManager::markOpened(const QString& path) {
    if (path.isEmpty())
        return;
    m_opened.insert(QDir::cleanPath(path), QDateTime::currentMSecsSinceEpoch());
    scheduleSave();
}

void FileMetadataManager::renamePath(const QString& oldPath, const QString& newPath) {
    if (oldPath.isEmpty() || newPath.isEmpty())
        return;
    const QString from = QDir::cleanPath(oldPath);
    const QString to = QDir::cleanPath(newPath);
    bool changed = false;
    if (m_added.contains(from)) {
        m_added.insert(to, m_added.take(from));
        changed = true;
    }
    if (m_opened.contains(from)) {
        m_opened.insert(to, m_opened.take(from));
        changed = true;
    }
    if (changed)
        scheduleSave();
}

void FileMetadataManager::forgetPath(const QString& path) {
    if (path.isEmpty())
        return;
    const QString clean = QDir::cleanPath(path);
    bool changed = false;
    changed = m_added.remove(clean) > 0 || changed;
    changed = m_opened.remove(clean) > 0 || changed;
    if (changed)
        scheduleSave();
}

void FileMetadataManager::forgetPaths(const QStringList& paths) {
    bool changed = false;
    for (const QString& path : paths) {
        const QString clean = QDir::cleanPath(path);
        changed = m_added.remove(clean) > 0 || changed;
        changed = m_opened.remove(clean) > 0 || changed;
    }
    if (changed)
        scheduleSave();
}
