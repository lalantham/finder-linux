#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class MainWindow;

// Implements the freedesktop.org org.freedesktop.Application interface on the
// bus name that matches our desktop file (org.finderlinux.Finder). Combined
// with DBusActivatable=true this gives launchers a standard way to start, raise
// and open files in Finder.
class ApplicationService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit ApplicationService(MainWindow& window, QObject* parent = nullptr);

    // Exports /org/finderlinux/Finder and owns the org.finderlinux.Finder
    // bus name (the object path is the desktop ID with dots replaced by '/').
    bool registerOnBus();

    // Writes the user-scoped activation file used by launchers to start Finder.
    static bool installActivationFile(QString* error = nullptr);

public slots:
    void Activate(const QVariantMap& platformData);
    void Open(const QStringList& uris, const QVariantMap& platformData);
    void ActivateAction(const QString& actionName, const QVariantList& parameter,
                        const QVariantMap& platformData);

private:
    MainWindow& m_window;
};
