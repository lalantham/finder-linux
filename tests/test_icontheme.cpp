#include "IconTheme.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// The precedence rule that matters: a theme the user configured must beat a
// theme Qt merely guessed. Qt guesses "breeze" on KDE hosts when it cannot load
// the platform theme, which is exactly what happens inside an AppImage, and
// trusting that guess made the AppImage show Breeze icons instead of the user's.
class TestIconTheme : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void testConfiguredThemeBeatsQtGuess();
    void testQtGuessUsedWhenNothingConfigured();
    void testMissingConfiguredThemeFallsThrough();
    void testConfiguredThemeFromKdeglobals();
    void testConfiguredThemeFromGtk();
    void testGtk4WinsOverGtk3();
    void testConfiguredThemeIgnoredWhenNotInstalled();
    void testNoThemeAtAllReturnsEmpty();
    void testThemeAvailabilityRequiresIndexTheme();

private:
    // Builds a fake home containing the given icon themes, plus an ini file.
    QString makeTheme(const QString& home, const QString& theme);
    void writeIni(const QString& path, const QString& key, const QString& value);

    QTemporaryDir m_dir;
};

void TestIconTheme::initTestCase() {
    QVERIFY(m_dir.isValid());
}

QString TestIconTheme::makeTheme(const QString& home, const QString& theme) {
    const QString root = home + QStringLiteral("/.local/share/icons/") + theme;
    const bool created = QDir().mkpath(root);
    Q_ASSERT(created);
    Q_UNUSED(created);
    QFile index(root + QStringLiteral("/index.theme"));
    if (index.open(QIODevice::WriteOnly)) {
        index.write("[Icon Theme]\nName=" + theme.toUtf8() + "\n");
        index.close();
    }
    return root;
}

void TestIconTheme::writeIni(const QString& path, const QString& key, const QString& value) {
    const bool created = QDir().mkpath(QFileInfo(path).absolutePath());
    Q_UNUSED(created);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    const QString section = key.section(QLatin1Char('/'), 0, 0);
    const QString name = key.section(QLatin1Char('/'), 1);
    file.write(QStringLiteral("[%1]\n%2=%3\n").arg(section, name, value).toUtf8());
    file.close();
}

void TestIconTheme::testConfiguredThemeBeatsQtGuess() {
    // The reported bug, in one assertion: Qt guessed breeze, the user chose Tela.
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString root = home.path() + QStringLiteral("/.local/share/icons");
    makeTheme(home.path(), QStringLiteral("Tela-dark"));
    makeTheme(home.path(), QStringLiteral("breeze"));

    // Availability must be judged against real directories, not a guessed path.
    QCOMPARE(IconTheme::resolveTheme(QStringLiteral("Tela-dark"), QStringLiteral("breeze"), {root}),
             QStringLiteral("Tela-dark"));
}

void TestIconTheme::testQtGuessUsedWhenNothingConfigured() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString root = home.path() + QStringLiteral("/.local/share/icons");
    makeTheme(home.path(), QStringLiteral("Papirus"));

    // Nothing configured: keep whatever the platform theme resolved.
    QCOMPARE(IconTheme::resolveTheme(QString(), QStringLiteral("Papirus"), {root}),
             QStringLiteral("Papirus"));
}

void TestIconTheme::testMissingConfiguredThemeFallsThrough() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString root = home.path() + QStringLiteral("/.local/share/icons");
    makeTheme(home.path(), QStringLiteral("Adwaita"));

    // A configured theme that is not installed must not win.
    QCOMPARE(
        IconTheme::resolveTheme(QStringLiteral("Uninstalled"), QStringLiteral("Adwaita"), {root}),
        QStringLiteral("Adwaita"));
}

void TestIconTheme::testConfiguredThemeFromKdeglobals() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    makeTheme(home.path(), QStringLiteral("Tela-dark"));
    writeIni(home.path() + QStringLiteral("/.config/kdeglobals"), QStringLiteral("Icons/Theme"),
             QStringLiteral("Tela-dark"));

    QCOMPARE(IconTheme::configuredTheme(home.path()), QStringLiteral("Tela-dark"));
}

void TestIconTheme::testConfiguredThemeFromGtk() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    makeTheme(home.path(), QStringLiteral("Papirus-Dark"));
    writeIni(home.path() + QStringLiteral("/.config/gtk-3.0/settings.ini"),
             QStringLiteral("Settings/gtk-icon-theme-name"), QStringLiteral("Papirus-Dark"));

    QCOMPARE(IconTheme::configuredTheme(home.path()), QStringLiteral("Papirus-Dark"));
}

void TestIconTheme::testGtk4WinsOverGtk3() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    makeTheme(home.path(), QStringLiteral("Newer"));
    makeTheme(home.path(), QStringLiteral("Stale"));
    writeIni(home.path() + QStringLiteral("/.config/gtk-4.0/settings.ini"),
             QStringLiteral("Settings/gtk-icon-theme-name"), QStringLiteral("Newer"));
    writeIni(home.path() + QStringLiteral("/.config/gtk-3.0/settings.ini"),
             QStringLiteral("Settings/gtk-icon-theme-name"), QStringLiteral("Stale"));

    QCOMPARE(IconTheme::configuredTheme(home.path()), QStringLiteral("Newer"));
}

void TestIconTheme::testConfiguredThemeIgnoredWhenNotInstalled() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    // Configured but never installed: must not be returned.
    writeIni(home.path() + QStringLiteral("/.config/kdeglobals"), QStringLiteral("Icons/Theme"),
             QStringLiteral("GhostTheme"));
    QCOMPARE(IconTheme::configuredTheme(home.path()), QString());
}

void TestIconTheme::testNoThemeAtAllReturnsEmpty() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    QCOMPARE(IconTheme::configuredTheme(home.path()), QString());
    QCOMPARE(IconTheme::resolveTheme(QString(), QString(), {}), QString());
}

void TestIconTheme::testThemeAvailabilityRequiresIndexTheme() {
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString root = home.path() + QStringLiteral("/.local/share/icons/HalfBaked");
    QVERIFY(QDir().mkpath(root));
    // Directory exists but has no index.theme: not a usable theme.
    QVERIFY(!IconTheme::themeAvailable(QStringLiteral("HalfBaked"),
                                       {home.path() + QStringLiteral("/.local/share/icons")}));

    QFile index(root + QStringLiteral("/index.theme"));
    QVERIFY(index.open(QIODevice::WriteOnly));
    index.write("[Icon Theme]\n");
    index.close();
    QVERIFY(IconTheme::themeAvailable(QStringLiteral("HalfBaked"),
                                      {home.path() + QStringLiteral("/.local/share/icons")}));
}

QTEST_MAIN(TestIconTheme)
#include "test_icontheme.moc"
