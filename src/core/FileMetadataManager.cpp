#include "FileMetadataManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

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

    QJsonObject added;
    for (auto it = m_added.cbegin(); it != m_added.cend(); ++it) {
        added.insert(it.key(), static_cast<double>(it.value()));
    }
    QJsonObject opened;
    for (auto it = m_opened.cbegin(); it != m_opened.cend(); ++it) {
        opened.insert(it.key(), static_cast<double>(it.value()));
    }
    QJsonObject root;
    root.insert(QStringLiteral("added"), added);
    root.insert(QStringLiteral("opened"), opened);

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
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
