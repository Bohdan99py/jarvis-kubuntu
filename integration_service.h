#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>

#include "desktop_integration.h"
#include "jv_sys.h"
#include "systemd_notify.h"
#include "telemetry_service.h"
#include "workspace_service.h"

class Notifier;

namespace jarvis {

class Assistant;

// KRunner's D-Bus types (org.kde.krunner1).
struct RunnerMatch
{
    QString id;
    QString text;
    QString icon;
    int type = 100;          // KRunner's ExactMatch: shown first
    double relevance = 1.0;
    QVariantMap properties;  // subtext, actions…
};
struct RunnerAction
{
    QString id;
    QString text;
    QString icon;
};
using RunnerMatches = QList<RunnerMatch>;
using RunnerActions = QList<RunnerAction>;

QDBusArgument &operator<<(QDBusArgument &arg, const RunnerMatch &m);
const QDBusArgument &operator>>(const QDBusArgument &arg, RunnerMatch &m);
QDBusArgument &operator<<(QDBusArgument &arg, const RunnerAction &a);
const QDBusArgument &operator>>(const QDBusArgument &arg, RunnerAction &a);

// jarvisd's side of the KDE integration (see desktop_integration.h for the
// privilege tiers): org.jarvis.Integration1 for the Plasma widget and scripts,
// a KRunner runner, the daemon's life without the Jarvis window ("background
// work") and its health for systemd (watchdog and status line).
//
// 0.19.2: the desktop shell's data — telemetry for the widgets (one cache for
// all of them), the workspaces, media control, the last events, the voice
// state the window reports and the heartbeats of the dock and the core (the
// shell's check that they really run).
class IntegrationService : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(IntegrationService)

public:
    // `gui(method, args)` calls org.jarvis.Desktop1 (Open, Launch, OpenPath, OpenSettings), starting the window if needed.
    using GuiCall = WorkspaceService::GuiCall;
    IntegrationService(Assistant *assistant, Notifier *notifier, GuiCall gui, QObject *parent = nullptr);

    // Exports the objects and, with the integration on, the runner's bus name.
    bool start(QString *error);
    void stopping();

    QString statusJson();
    QString metricsJson();
    // These four return "" or a D-Bus error was already sent (and the text says why).
    QString runAction(const QString &id);
    QString activeApplication();
    QString notify(const QString &title, const QString &body);
    QString telemetry(const QString &kinds);
    QString mediaControl(const QString &action);
    QString setPowerProfile(const QString &profile);
    QString switchWorkspace(const QString &id);
    QString workspaces();
    QString saveWorkspaceLayout();
    void workspaceReport(const QString &json);
    void reportVoice(bool listening, bool speaking, const QString &engine);
    void componentAlive(const QString &component);
    QString eventsJson() const;

    // KRunner.
    RunnerMatches match(const QString &query);
    RunnerActions runnerActions() const;
    void run(const QString &matchId, const QString &actionId);

signals:
    void statusChanged(const QString &json);
    void noticePosted(const QString &json);
    void eventPosted(const QString &json);

private:
    QString denied(const QString &why);
    QString caller() const;
    bool enabled() const;
    void touch();
    void emitStatus();
    void applyConfig();
    void updateLifetime();
    void notePosted(const QString &kind, const QString &title);
    void pushEvent(const QString &kind, const QString &text);
    bool callerIsKWin() const;
    bool callerIsGui() const;
    bool acting(const QString &what); // integration on and within the rate limit; else an error was sent

    Assistant *m_assistant;
    Notifier *m_notifier;
    GuiCall m_callGui;
    TelemetryService m_telemetry;
    WorkspaceService m_workspaces;
    QDBusServiceWatcher m_guiWatcher;
    systemd::Watchdog m_watchdog;
    integration::RateLimiter m_actionLimit{4, 2000};
    integration::RateLimiter m_notifyLimit{3, 20000};
    integration::RateLimiter m_notifyAll{6, 10000};
    QTimer m_statusDelay;     // coalesces status changes
    QTimer m_stateExpiry;     // "notification" and "error" fade back to idle
    QTimer m_lifetime;        // background work off: exit without the window
    QSet<quint64> m_requests; // requests being answered
    QJsonObject m_notice;     // {kind, title, at}
    QJsonArray m_events;      // the last 12: {at, kind, text}
    QJsonObject m_voice;      // {listening, speaking, engine, at} from the window
    QJsonObject m_components; // dock, core… -> last heartbeat (ms)
    QHash<quint64, qint64> m_started; // request -> when it started
    QStringList m_routes;     // of the request being answered (or the last one)
    int m_requestsTotal = 0;
    int m_errorsTotal = 0;
    double m_latency = 0;     // ms, the last answer
    qint64 m_errorAt = 0;
    qint64 m_startedAt = 0;
    QString m_lastStatus;
    jv_cpu_times m_cpuPrev{};
    QElapsedTimer m_cpuAge;
    double m_lastCpu = -1;
    bool m_cpuValid = false;
    bool m_guiSeen = false; // the window was open at some point: a short grace after it closes
    bool m_onBattery = false;
    bool m_gui = false;
    bool m_runnerOwned = false;
};

// interface org.jarvis.Integration1 at /org/jarvis/Integration1
//   method Status() -> s            state JSON (observe)
//   method Actions() -> s           [{id, label, icon}] (observe)
//   method SystemMetrics() -> s     CPU, memory, temperature, battery JSON (observe)
//   method RunAction(s) -> s        one of Actions(); "" = done (act)
//   method OpenOverlay() -> s       RunAction("overlay") (act)
//   method Notify(s, s) -> s        a desktop notification from Jarvis, plain text, rate limited (act)
//   method ActiveApplication() -> s the focused application's name, with consent (personal)
//   method Telemetry(s) -> s        "cpu,gpu,…" -> {cpu: {…}, gpu: {…}} (observe; cached for every widget)
//   method MediaControl(s) -> s     play-pause, next, previous, volume-up, volume-down, mute (act)
//   method SetPowerProfile(s) -> s  power-saver, balanced, performance (act; polkit allows it in the active session)
//   method Workspace(s) -> s        switch to general|study|work|dev, "prepare:<id>", "previous" (act)
//   method Workspaces() -> s        {current, previous, profiles} (observe)
//   method SaveWorkspaceLayout() -> s  remember where the current profile's windows are (act)
//   method WorkspaceReport(s)       the workspace KWin script's answer (KWin only)
//   method ReportVoice(b, b, s)     listening, speaking, voice engine (the Jarvis window only)
//   method ComponentAlive(s)        a heartbeat of the desktop shell's parts: core, dock, … (observe)
//   method Events() -> s            the last events: [{at, kind, text}] (observe)
//   signal StatusChanged(s)         Status() changed
//   signal NoticePosted(s)          a notification was shown: {kind, title, at}
//   signal EventPosted(s)           something happened: {at, kind, text} (requests, answers, workspaces…)
class IntegrationAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jarvis.Integration1")

public:
    explicit IntegrationAdaptor(IntegrationService *service);

public slots:
    QString Status();
    QString Actions();
    QString SystemMetrics();
    QString RunAction(const QString &id);
    QString OpenOverlay();
    QString Notify(const QString &title, const QString &body);
    QString ActiveApplication();
    QString Telemetry(const QString &kinds);
    QString MediaControl(const QString &action);
    QString SetPowerProfile(const QString &profile);
    QString Workspace(const QString &id);
    QString Workspaces();
    QString SaveWorkspaceLayout();
    void WorkspaceReport(const QString &json);
    void ReportVoice(bool listening, bool speaking, const QString &engine);
    void ComponentAlive(const QString &component);
    QString Events();

signals:
    void StatusChanged(const QString &json);
    void NoticePosted(const QString &json);
    void EventPosted(const QString &json);

private:
    IntegrationService *m_service;
};

// interface org.kde.krunner1 at /org/jarvis/KRunner, on the bus name
// org.jarvis.Runner (not D-Bus activatable: KRunner never starts jarvisd).
//   "jarvis <question>" (or "джарвис …"): ask Jarvis in the overlay, or add it as a task.
class RunnerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    RunnerAdaptor(QObject *object, IntegrationService *service);

public slots:
    jarvis::RunnerActions Actions();
    jarvis::RunnerMatches Match(const QString &query);
    void Run(const QString &matchId, const QString &actionId);
    QVariantMap Config();
    void Teardown();

private:
    IntegrationService *m_service;
};

} // namespace jarvis

Q_DECLARE_METATYPE(jarvis::RunnerMatch)
Q_DECLARE_METATYPE(jarvis::RunnerAction)
