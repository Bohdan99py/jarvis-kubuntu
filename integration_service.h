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
class IntegrationService : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(IntegrationService)

public:
    // `openGui(target, payload)` shows a part of the Jarvis window, starting it if needed.
    IntegrationService(Assistant *assistant, Notifier *notifier, std::function<void(QString, QString)> openGui,
                       QObject *parent = nullptr);

    // Exports the objects and, with the integration on, the runner's bus name.
    bool start(QString *error);
    void stopping();

    QString statusJson();
    QString metricsJson();
    // These four return "" or a D-Bus error was already sent (and the text says why).
    QString runAction(const QString &id);
    QString activeApplication();
    QString notify(const QString &title, const QString &body);

    // KRunner.
    RunnerMatches match(const QString &query);
    RunnerActions runnerActions() const;
    void run(const QString &matchId, const QString &actionId);

signals:
    void statusChanged(const QString &json);
    void noticePosted(const QString &json);

private:
    QString denied(const QString &why);
    QString caller() const;
    bool enabled() const;
    void touch();
    void emitStatus();
    void applyConfig();
    void updateLifetime();
    void notePosted(const QString &kind, const QString &title);

    Assistant *m_assistant;
    Notifier *m_notifier;
    std::function<void(QString, QString)> m_openGui;
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
//   signal StatusChanged(s)         Status() changed
//   signal NoticePosted(s)          a notification was shown: {kind, title, at}
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

signals:
    void StatusChanged(const QString &json);
    void NoticePosted(const QString &json);

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
