#pragma once

#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

// The GUI's side of the KDE integration (Control center → Desktop):
//  * the desktop widget: is it installed, how many are on the desktop, adding
//    one and removing Jarvis's own (only after the user confirmed in the
//    dialog; panels, other widgets and wallpapers are never touched);
//  * diagnostics of the services: jarvis.service and jarvis-tray.service
//    through systemd's D-Bus API, the daemon's answer time and version,
//    Plasma, the widget and the KRunner runner;
//  * restoring the standard desktop: Jarvis widgets removed, the integration
//    off. Also from a terminal (`jarvis --restore-desktop`) when the window
//    itself does not start.
// Every D-Bus call is asynchronous: the interface never waits for Plasma or systemd.
class PlasmaBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool plasmaRunning READ plasmaRunning NOTIFY changed)
    Q_PROPERTY(bool widgetInstalled READ widgetInstalled NOTIFY changed)
    // Jarvis widgets on desktops and panels; -1 while unknown.
    Q_PROPERTY(int widgetCount READ widgetCount NOTIFY changed)
    Q_PROPERTY(bool runnerInstalled READ runnerInstalled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // [{name, level: ok|warn|error|off, text}]
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString report READ report NOTIFY diagnosticsChanged)

public:
    explicit PlasmaBridge(QObject *parent = nullptr);

    bool plasmaRunning() const { return m_plasma; }
    bool widgetInstalled() const;
    int widgetCount() const { return m_widgets; }
    bool runnerInstalled() const;
    bool busy() const { return m_busy > 0; }
    QString status() const { return m_status; }
    QVariantList diagnostics() const { return m_rows; }
    QString report() const;

    // Widget count and diagnostics.
    Q_INVOKABLE void refresh();
    // A Jarvis widget id (org.jarvis.hud, .system, …); removeWidgets("") removes every Jarvis widget.
    Q_INVOKABLE void addWidget(const QString &type = QStringLiteral("org.jarvis.hud"));
    Q_INVOKABLE void removeWidgets(const QString &type = QStringLiteral("org.jarvis.hud"));
    Q_INVOKABLE void copyReport() const;

    // For the terminal: run diagnostics / the restore and print the result.
    // Blocks (with time limits); returns the process exit status.
    static int printDiagnostics();
    // The standard desktop back: Jarvis widgets removed, desktop integration and
    // active-application sharing off, the daemon told to re-read its settings.
    // (The window does the same through AppSettings and removeWidgets().)
    static int restoreFromTerminal();

signals:
    void changed();
    void diagnosticsChanged();
    // A restore or a removal finished; `removed` widgets are gone.
    void restored(int removed);

private:
    using Reply = std::function<void(const QDBusMessage &)>;
    void call(const QDBusMessage &msg, const Reply &done, int timeoutMs = 4000);
    void runScript(const QString &script, const std::function<void(int, QString)> &done);
    void queryUnit(const QString &key, const QString &unit, bool wanted);
    void setRow(const QString &key, const QString &name, const QString &level, const QString &text);
    void finishStep();
    static bool disableIntegration(QString *error);

    QDBusServiceWatcher m_plasmaWatcher;
    QHash<QString, QVariantMap> m_found;
    QVariantList m_rows;
    QString m_status;
    int m_widgets = -1;
    int m_busy = 0;
    int m_pending = 0;
    bool m_plasma = false;
};
