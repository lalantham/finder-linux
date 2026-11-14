#pragma once

#include <QObject>
#include <QStringList>

class MainWindow;

// Implements the freedesktop.org org.freedesktop.FileManager1 D-Bus
// interface. Browsers and other applications use ShowItems() for
// "Show in folder" / "Open containing folder"; without this interface they
// fall back to whichever other file manager happens to own the name.
class FileManager1Service : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManager1Service(MainWindow& window, QObject* parent = nullptr);

    // Exports /org/freedesktop/FileManager1 and requests the well-known name.
    // Returns true when File Manager currently owns the name.
    bool registerOnBus();

    // Writes the user-scoped D-Bus activation file used by browsers when
    // File Manager is not running yet. Safe to call repeatedly.
    static bool installActivationFile(QString* error = nullptr);

public slots:
    // URIs are file:// URIs as required by the specification.
    void ShowItems(const QStringList& uris, const QString& startupId);
    void ShowItemProperties(const QStringList& uris, const QString& startupId);

private:
    MainWindow& m_window;
};
