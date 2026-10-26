#pragma once

#include <QDBusMessage>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

// JARVIS DESKTOP (0.19.2): switches the Plasma desktop between STANDARD KDE
// and JARVIS DESKTOP, safely.
//
//  check()   is Plasma 6 here, are the widgets and the visual module installed,
//            are widgets unlocked, does jarvisd answer; then the plan (what
//            exactly changes) for the preview. Changes nothing.
//  apply()   only after the user confirmed the preview:
//             1. backs up Plasma's layout files (appletsrc, plasmashellrc) and
//                the layout script Plasma itself dumps — the first backup is the
//                restore point and is never overwritten;
//             2. adds the Jarvis parts (core, widgets, dock panel with Plasma's
//                tray and clock) and records their ids;
//             3. verifies: every part exists, and the dock and the core report
//                to jarvisd that they really run;
//             4. only then removes the standard panels (if the user wants);
//            any failure takes the Jarvis parts away again — the standard panels
//            were never touched.
//  restore() puts the backed-up layout files back while plasmashell is stopped
//            and starts it again: the desktop exactly as it was. Without a
//            backup it removes the Jarvis parts only.
//  update()  applies new dock and core settings to Jarvis's own panel and core.
//  dock      (0.21) the dock as Plasma has it: its panel and its own settings,
//            read from Plasma and written to Plasma (setDock), so the settings
//            window and the dock's context menu show one value. Changes made in
//            Plasma (edit mode, the dock's settings) are noticed (watchDock) and
//            the panel's values go back into config.json.
// The same from a terminal: `jarvis --desktop-mode jarvis|standard`,
// `jarvis --restore-desktop`. The state lives in ~/.local/share/jarvis/desktop-state.json.
class DesktopShell : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(bool compatible READ compatible NOTIFY changed)
    // [{name, ok, text}]
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed)
    // The changes apply() makes, for people.
    Q_PROPERTY(QStringList plan READ plan NOTIFY changed)
    Q_PROPERTY(bool hasBackup READ hasBackup NOTIFY changed)
    Q_PROPERTY(QString backupPath READ backupPath NOTIFY changed)
    // Workspaces for the Control center: [{id, label, desktop, apps, paths, widgets, focus}].
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    // {found, reading, error, dockPosition, dockSize, dockAlignment, dockAutohide,
    //  iconSize, magnification, magnifyAmount, panelStyle, showProfiles,
    //  showDesktops, showJarvis, groupWindows}
    Q_PROPERTY(QVariantMap dock READ dock NOTIFY dockChanged)
    // While true, Plasma's layout file is watched and the dock read again on change.
    Q_PROPERTY(bool watchDock READ watchDock WRITE setWatchDock NOTIFY dockChanged)

public:
    explicit DesktopShell(QObject *parent = nullptr);

    QString mode() const;
    bool busy() const { return !m_step.isEmpty(); }
    QString status() const { return m_status; }
    bool checked() const { return m_checked; }
    bool compatible() const { return m_compatible; }
    QVariantList checks() const { return m_checks; }
    QStringList plan() const { return m_planText; }
    bool hasBackup() const;
    QString backupPath() const;

    Q_INVOKABLE void check();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void restore();
    Q_INVOKABLE void update();
    // KDE virtual desktops for the workspaces (one per profile), named after them;
    // only missing ones are created, and a restore removes them again.
    Q_INVOKABLE void createWorkspaceDesktops();
    QVariantList profiles() const;
    // key: apps, paths, widgets (lists), desktop (number), label. "" or why not.
    Q_INVOKABLE QString setProfile(const QString &id, const QString &key, const QVariant &value);
    // Installed applications for a profile's list (0.19.3): [{id, name, icon}] matching the words.
    Q_INVOKABLE QVariantList findApps(const QString &words) const;
    // The name and icon of a profile entry ("a|b": the first installed); {id, name, icon, installed}.
    Q_INVOKABLE QVariantMap describeApp(const QString &alternatives) const;
    // Plasma's own edit mode: move and resize widgets on the desktop.
    Q_INVOKABLE void setEditMode(bool on);

    QVariantMap dock() const { return m_dock; }
    bool watchDock() const { return m_watchDock; }
    void setWatchDock(bool on);
    Q_INVOKABLE void readDock();
    // A panel setting (dockPosition, dockSize, dockAlignment, dockAutohide: saved
    // in config.json and written to the dock's panel) or one of the dock's own
    // settings (shell::dockConfigKeys). "" or why not; Plasma's answer arrives in `dock`.
    Q_INVOKABLE QString setDock(const QString &key, const QVariant &value);
    // The dock's own look back to its defaults (pinned applications stay).
    Q_INVOKABLE void resetDockLook();

    static QString statePath();
    static QJsonObject loadState();
    static bool saveState(const QJsonObject &state);
    // Terminal: blocking, prints what happens; exit status 0 on success.
    static int runFromTerminal(const QString &what);

signals:
    void changed();
    void dockChanged();
    // apply/restore/check finished.
    void finished(bool ok, const QString &message);

private:
    using Reply = std::function<void(const QDBusMessage &)>;
    void call(const QDBusMessage &msg, const Reply &done, int timeoutMs = 8000);
    void script(const QString &js, const std::function<void(bool ok, const QJsonObject &printed, const QString &error)> &done);
    void setStep(const QString &step, const QString &status);
    void finish(bool ok, const QString &message);
    bool backup(QString *error);
    void verify(const QJsonObject &created, qint64 appliedAt, int attempt);
    void rollback(const QJsonObject &created, const QString &why);
    void setMode(const QString &mode);
    void stopPlasma(const std::function<void(bool viaSystemd)> &stopped);
    void startPlasma(bool viaSystemd);
    void waitForPlasma(bool wanted, int attempts, const std::function<void(bool)> &done);
    bool russian() const;
    void runDock(const QJsonObject &changes);

    QString m_step;
    QString m_status;
    bool m_checked = false;
    bool m_compatible = false;
    QVariantList m_checks;
    QStringList m_planText;
    QJsonObject m_inspection;
    QJsonObject m_plan;
    QVariantMap m_dock;
    bool m_watchDock = false;
    QFileSystemWatcher m_layoutWatcher;
    QTimer m_layoutChanged;
    QDateTime m_layoutStamp;
};
