#include "AppSettings.h"
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

// Covers the persistence and resolution rules for per-folder view modes:
// override-for-this-exact-folder, otherwise the global default.
class TestViewModes : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void testGlobalDefaultFallback();
    void testOverrideWins();
    void testNoInheritanceToSubfolders();
    void testClearOverride();
    void testPathNormalisation();
    void testPersistenceRoundTrip();
    void testLegacyIntEncodingReadable();
    void testLegacyMigrationEnablesOverrides();
    void testDisabledOverridesIgnoreStored();
    void testVirtualPathsRejected();
    void testLruPrune();
    void testPruneDropsVirtualPaths();
    void testScopePreferencePersistence();

private:
    QString freshSettingsFile();

    QTemporaryDir m_settingsDir;
    int m_fileCounter{0};
};

void TestViewModes::initTestCase() {
    QVERIFY(m_settingsDir.isValid());
}

void TestViewModes::init() {
    AppSettings::setSettingsFilePath(freshSettingsFile());
}

void TestViewModes::cleanupTestCase() {
    AppSettings::setSettingsFilePath(QString());
}

// A new ini path per test keeps the monotonic stamps and keys independent.
QString TestViewModes::freshSettingsFile() {
    return m_settingsDir.filePath(QStringLiteral("viewmodes-%1.ini").arg(++m_fileCounter));
}

void TestViewModes::testGlobalDefaultFallback() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);

    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/docs")), ViewMode::Column);
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/docs")));
}

void TestViewModes::testOverrideWins() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewMode(QStringLiteral("/tmp/photos"), ViewMode::Gallery);

    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/photos")), ViewMode::Gallery);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/docs")), ViewMode::Column);
    QVERIFY(settings.hasFolderViewModeOverride(QStringLiteral("/tmp/photos")));
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/docs")));
}

void TestViewModes::testNoInheritanceToSubfolders() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewMode(QStringLiteral("/tmp/photos"), ViewMode::Gallery);

    // The decision: overrides apply to the exact folder only.
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/photos/2024")), ViewMode::Column);
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/photos/2024")));
}

void TestViewModes::testClearOverride() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewMode(QStringLiteral("/tmp/photos"), ViewMode::Gallery);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/photos")), ViewMode::Gallery);

    settings.clearFolderViewMode(QStringLiteral("/tmp/photos"));
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/photos")), ViewMode::Column);
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/photos")));
}

void TestViewModes::testPathNormalisation() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Icon);
    // A trailing slash must not create a second key.
    settings.setFolderViewMode(QStringLiteral("/tmp/a/"), ViewMode::List);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/a")), ViewMode::List);
}

void TestViewModes::testPersistenceRoundTrip() {
    const QString file = freshSettingsFile();
    AppSettings::setSettingsFilePath(file);
    {
        auto& settings = AppSettings::instance();
        settings.setViewMode(ViewMode::Gallery);
        settings.setFolderViewMode(QStringLiteral("/tmp/one"), ViewMode::Column);
        settings.setFolderViewMode(QStringLiteral("/tmp/two"), ViewMode::List);
        settings.save();
    }

    AppSettings::setSettingsFilePath(file);
    auto& settings = AppSettings::instance();
    QCOMPARE(settings.viewMode(), ViewMode::Gallery);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/one")), ViewMode::Column);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/two")), ViewMode::List);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/three")), ViewMode::Gallery);
}

void TestViewModes::testLegacyIntEncodingReadable() {
    const QString file = freshSettingsFile();
    {
        // Builds before this feature stored a bare int per folder.
        QSettings raw(file, QSettings::IniFormat);
        QVariantMap legacy;
        legacy.insert(QStringLiteral("/tmp/old"), 3);
        raw.setValue(QStringLiteral("folderViewModes"), legacy);
        raw.setValue(QStringLiteral("settingsVersion"), 2);
        raw.sync();
    }

    AppSettings::setSettingsFilePath(file);
    QCOMPARE(AppSettings::instance().viewModeForPath(QStringLiteral("/tmp/old")),
             ViewMode::Gallery);
}

void TestViewModes::testLegacyMigrationEnablesOverrides() {
    const QString file = freshSettingsFile();
    {
        // A pre-v2 install: the gate existed but defaulted to off.
        QSettings raw(file, QSettings::IniFormat);
        raw.setValue(QStringLiteral("rememberFolderViewModes"), false);
        raw.sync();
    }

    AppSettings::setSettingsFilePath(file);
    QVERIFY(AppSettings::instance().folderViewModesEnabled());

    // The migration is one-shot: a deliberate "false" afterwards must stick.
    AppSettings::instance().setFolderViewModesEnabled(false);
    AppSettings::instance().save();
    AppSettings::setSettingsFilePath(file);
    QVERIFY(!AppSettings::instance().folderViewModesEnabled());
}

void TestViewModes::testDisabledOverridesIgnoreStored() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);
    settings.setFolderViewMode(QStringLiteral("/tmp/photos"), ViewMode::Gallery);

    settings.setFolderViewModesEnabled(false);
    // Stored but ignored...
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/photos")), ViewMode::Column);
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/photos")));

    // ...and switching back on restores it, so nothing was destroyed.
    settings.setFolderViewModesEnabled(true);
    QCOMPARE(settings.viewModeForPath(QStringLiteral("/tmp/photos")), ViewMode::Gallery);
}

void TestViewModes::testVirtualPathsRejected() {
    auto& settings = AppSettings::instance();
    settings.setViewMode(ViewMode::Column);

    const QStringList virtualPaths{QString(),
                                   QStringLiteral("tag://Red"),
                                   QStringLiteral("smart://Recent"),
                                   QStringLiteral("recent://Recents"),
                                   QStringLiteral("apps://All"),
                                   QStringLiteral("sftp://host/path")};
    for (const QString& path : virtualPaths) {
        settings.setFolderViewMode(path, ViewMode::Gallery);
        QVERIFY2(!settings.hasFolderViewModeOverride(path),
                 qPrintable(QStringLiteral("virtual path stored an override: %1").arg(path)));
        QCOMPARE(settings.viewModeForPath(path), ViewMode::Column);
    }
}

void TestViewModes::testLruPrune() {
    auto& settings = AppSettings::instance();
    for (const QString& path :
         {QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b"), QStringLiteral("/tmp/c"),
          QStringLiteral("/tmp/d"), QStringLiteral("/tmp/e")}) {
        settings.setFolderViewMode(path, ViewMode::List);
    }

    QCOMPARE(settings.pruneFolderViewModes(3), 2);
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/a")));
    QVERIFY(!settings.hasFolderViewModeOverride(QStringLiteral("/tmp/b")));
    QVERIFY(settings.hasFolderViewModeOverride(QStringLiteral("/tmp/c")));
    QVERIFY(settings.hasFolderViewModeOverride(QStringLiteral("/tmp/e")));

    // A cap of 0 means "no cap", not "delete everything".
    QCOMPARE(settings.pruneFolderViewModes(0), 0);
    QVERIFY(settings.hasFolderViewModeOverride(QStringLiteral("/tmp/c")));
}

void TestViewModes::testPruneDropsVirtualPaths() {
    const QString file = freshSettingsFile();
    {
        // Only a hand-edited ini could contain this.
        QSettings raw(file, QSettings::IniFormat);
        QVariantMap map;
        map.insert(QStringLiteral("tag://Red"), 1);
        map.insert(QStringLiteral("/tmp/real"), 1);
        raw.setValue(QStringLiteral("folderViewModes"), map);
        raw.setValue(QStringLiteral("settingsVersion"), 2);
        raw.sync();
    }

    AppSettings::setSettingsFilePath(file);
    QCOMPARE(AppSettings::instance().pruneFolderViewModes(100), 1);
    QVERIFY(AppSettings::instance().hasFolderViewModeOverride(QStringLiteral("/tmp/real")));
}

void TestViewModes::testScopePreferencePersistence() {
    const QString file = freshSettingsFile();
    AppSettings::setSettingsFilePath(file);
    QCOMPARE(AppSettings::instance().directViewModeScope(), ViewModeScope::CurrentFolder);

    AppSettings::instance().setDirectViewModeScope(ViewModeScope::AllFolders);
    AppSettings::instance().save();

    AppSettings::setSettingsFilePath(file);
    QCOMPARE(AppSettings::instance().directViewModeScope(), ViewModeScope::AllFolders);

    // An unknown value in the ini must fall back to the safe default.
    {
        QSettings raw(file, QSettings::IniFormat);
        raw.setValue(QStringLiteral("directViewModeScope"), QStringLiteral("nonsense"));
        raw.sync();
    }
    AppSettings::setSettingsFilePath(file);
    QCOMPARE(AppSettings::instance().directViewModeScope(), ViewModeScope::CurrentFolder);
}

QTEST_MAIN(TestViewModes)
#include "test_viewmodes.moc"
