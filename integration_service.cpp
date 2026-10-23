#include "integration_service.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>

#include "assistant.h"
#include "build_config.h"
#include "dbus_names.h"
#include "language.h"
#include "notifier.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr qint64 kErrorShownMs = 10000;
constexpr qint64 kNoticeShownMs = 8000;

qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString pick(Lang lang, const QString &ru, const QString &en)
{
    return lang == Lang::Ru ? ru : en;
}

// Without the window, a daemon whose user turned background work off waits
// this long for it (at login, or after the window was closed) before exiting.
int graceMs(bool guiSeen)
{
    const int env = qEnvironmentVariableIntValue("JARVIS_GUI_GRACE_MS");
    if (env > 0)
        return env;
    return guiSeen ? 10000 : 120000;
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &arg, const RunnerMatch &m)
{
    arg.beginStructure();
    arg << m.id << m.text << m.icon << m.type << m.relevance << m.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, RunnerMatch &m)
{
    arg.beginStructure();
    arg >> m.id >> m.text >> m.icon >> m.type >> m.relevance >> m.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const RunnerAction &a)
{
    arg.beginStructure();
    arg << a.id << a.text << a.icon;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, RunnerAction &a)
{
    arg.beginStructure();
    arg >> a.id >> a.text >> a.icon;
    arg.endStructure();
    return arg;
}

IntegrationService::IntegrationService(Assistant *assistant, Notifier *notifier,
                                       std::function<void(QString, QString)> openGui, QObject *parent)
    : QObject(parent)
    , m_assistant(assistant)
    , m_notifier(notifier)
    , m_openGui(std::move(openGui))
    , m_guiWatcher(u"org.jarvis.Desktop1"_s, QDBusConnection::sessionBus(),
                   QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)
{
    qDBusRegisterMetaType<RunnerMatch>();
    qDBusRegisterMetaType<RunnerAction>();
    qDBusRegisterMetaType<RunnerMatches>();
    qDBusRegisterMetaType<RunnerActions>();
    new IntegrationAdaptor(this);

    m_startedAt = now();
    jv_battery battery{};
    m_onBattery = jv_battery_read(&battery) == 0 && battery.present && QByteArray(battery.status) == "Discharging";
    m_cpuValid = jv_cpu_times_read(&m_cpuPrev) == 0;
    m_cpuAge.start();

    m_statusDelay.setSingleShot(true);
    m_statusDelay.setInterval(250);
    connect(&m_statusDelay, &QTimer::timeout, this, &IntegrationService::emitStatus);
    m_stateExpiry.setSingleShot(true);
    connect(&m_stateExpiry, &QTimer::timeout, this, &IntegrationService::touch);
    m_lifetime.setSingleShot(true);
    m_lifetime.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_lifetime, &QTimer::timeout, this, [this] {
        if (m_gui || m_assistant->config().background)
            return;
        if (!m_requests.isEmpty()) { // let the answer finish
            m_lifetime.start(graceMs(true));
            return;
        }
        qInfo() << "background work is off and the Jarvis window is closed: exiting";
        QCoreApplication::quit();
    });

    connect(&m_guiWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        m_gui = m_guiSeen = true;
        updateLifetime();
        touch();
    });
    connect(&m_guiWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        m_gui = false;
        updateLifetime();
        touch();
    });

    // What the desktop shows: requests in progress, failures, notifications.
    connect(m_assistant, &Assistant::routed, this, [this](quint64 id, const QString &) {
        if (id != 0) {
            m_requests.insert(id);
            touch();
        }
    });
    connect(m_assistant, &Assistant::replyReady, this, [this](quint64 id, const QString &) {
        if (m_requests.remove(id))
            touch();
    });
    connect(m_assistant, &Assistant::failed, this, [this](quint64 id, const QString &) {
        m_requests.remove(id);
        m_errorAt = now();
        m_stateExpiry.start(int(kErrorShownMs) + 50);
        touch();
    });
    connect(m_assistant, &Assistant::notice, this, [this](const QJsonObject &notice) {
        notePosted(notice.value(u"kind"_s).toString(), notice.value(u"title"_s).toString());
    });
    connect(m_assistant, &Assistant::contextChanged, this, &IntegrationService::touch);
    connect(m_assistant, &Assistant::configReloaded, this, &IntegrationService::applyConfig);
}

bool IntegrationService::start(QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(integration::kPath, this, QDBusConnection::ExportAdaptors)) {
        *error = u"cannot register %1: %2"_s.arg(integration::kPath, bus.lastError().message());
        return false;
    }
    auto *runner = new QObject(this);
    new RunnerAdaptor(runner, this);
    if (!bus.registerObject(integration::kRunnerPath, runner, QDBusConnection::ExportAdaptors))
        qWarning().noquote() << "KRunner object unavailable:" << bus.lastError().message(); // the rest keeps working
    m_gui = m_guiSeen = bus.interface() && bus.interface()->isServiceRegistered(u"org.jarvis.Desktop1"_s).value();
    applyConfig();
    emitStatus();
    systemd::notify("READY=1");
    return true;
}

void IntegrationService::stopping()
{
    m_watchdog.stopping();
}

void IntegrationService::applyConfig()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const bool want = m_assistant->config().desktopIntegration;
    if (want && !m_runnerOwned) {
        m_runnerOwned = bus.registerService(dbus::runnerService());
        if (!m_runnerOwned)
            qWarning().noquote() << "KRunner name unavailable:" << bus.lastError().message();
    } else if (!want && m_runnerOwned) {
        bus.unregisterService(dbus::runnerService());
        m_runnerOwned = false;
    }
    updateLifetime();
    touch();
}

void IntegrationService::updateLifetime()
{
    if (m_gui || m_assistant->config().background) {
        m_lifetime.stop();
        return;
    }
    if (!m_lifetime.isActive())
        m_lifetime.start(graceMs(m_guiSeen));
}

bool IntegrationService::enabled() const
{
    return m_assistant->config().desktopIntegration;
}

QString IntegrationService::caller() const
{
    return calledFromDBus() ? message().service() : u"jarvisd"_s;
}

QString IntegrationService::denied(const QString &why)
{
    if (calledFromDBus())
        sendErrorReply(QDBusError::AccessDenied, why);
    return why;
}

void IntegrationService::touch()
{
    if (!m_statusDelay.isActive())
        m_statusDelay.start();
}

void IntegrationService::notePosted(const QString &kind, const QString &title)
{
    m_notice = QJsonObject{{u"kind"_s, kind.left(20)}, {u"title"_s, title.left(200)}, {u"at"_s, double(now())}};
    if (enabled())
        emit noticePosted(compact(m_notice));
    m_stateExpiry.start(int(kNoticeShownMs) + 50);
    touch();
}

QString IntegrationService::statusJson()
{
    const ConfigData &cfg = m_assistant->config();
    const qint64 t = now();
    const QString state = !m_requests.isEmpty()                         ? u"processing"_s
                          : m_errorAt > 0 && t - m_errorAt < kErrorShownMs ? u"error"_s
                          : !m_notice.isEmpty() && t - qint64(m_notice.value(u"at"_s).toDouble()) < kNoticeShownMs
                              ? u"notification"_s
                              : u"idle"_s;
    const QJsonObject context = QJsonDocument::fromJson(m_assistant->contextJson().toUtf8()).object();
    QJsonObject o{{u"version"_s, QStringLiteral(JARVIS_VERSION)},
                  {u"state"_s, state},
                  {u"startedAt"_s, double(m_startedAt)},
                  {u"gui"_s, m_gui},
                  {u"background"_s, cfg.background},
                  {u"integration"_s, cfg.desktopIntegration},
                  {u"shareActiveApp"_s, cfg.shareActiveApp},
                  {u"claude"_s, m_assistant->claudeEnabled()},
                  {u"awareness"_s, QJsonObject{{u"level"_s, cfg.awarenessLevel},
                                               {u"paused"_s, context.value(u"pausedUntil"_s).toDouble() > 0},
                                               {u"dnd"_s, context.value(u"dnd"_s).toBool()}}},
                  {u"animation"_s, QJsonObject{{u"wanted"_s, cfg.hudAnimation},
                                               {u"effective"_s, integration::effectiveAnimation(cfg.hudAnimation, m_onBattery)},
                                               {u"onBattery"_s, m_onBattery}}}};
    // The last notification is what the desktop already showed; still, only for an enabled integration.
    if (cfg.desktopIntegration && !m_notice.isEmpty())
        o[u"notice"_s] = m_notice;
    return compact(o);
}

void IntegrationService::emitStatus()
{
    const QString json = statusJson();
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    m_watchdog.setStatus(u"%1 · window %2 · integration %3 · background %4"_s.arg(
        o.value(u"state"_s).toString(), m_gui ? u"open"_s : u"closed"_s,
        o.value(u"integration"_s).toBool() ? u"on"_s : u"off"_s, o.value(u"background"_s).toBool() ? u"on"_s : u"off"_s));
    if (json == m_lastStatus)
        return;
    m_lastStatus = json;
    emit statusChanged(json);
}

QString IntegrationService::metricsJson()
{
    jv_cpu_times current{};
    const bool ok = jv_cpu_times_read(&current) == 0;
    // Two calls close together would measure noise: keep the window at least a second.
    const bool fresh = m_cpuValid && m_cpuAge.elapsed() >= 1000;
    QJsonObject m = integration::systemMetrics(ok && fresh ? &m_cpuPrev : nullptr, ok ? &current : nullptr);
    if (ok && fresh) {
        m_lastCpu = m.value(u"cpu"_s).toDouble();
        m_cpuPrev = current;
        m_cpuAge.restart();
    } else if (ok && !m_cpuValid) {
        m_cpuPrev = current;
        m_cpuValid = true;
        m_cpuAge.restart();
    } else {
        m.insert(u"cpu"_s, m_lastCpu);
    }
    const bool battery = integration::onBattery(m);
    if (battery != m_onBattery) {
        m_onBattery = battery;
        touch(); // the HUD's animation steps down or back up
    }
    return compact(m);
}

QString IntegrationService::runAction(const QString &id)
{
    if (!integration::isAction(id)) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::InvalidArgs, u"unknown action; see Actions()"_s);
        return u"unknown action"_s;
    }
    if (!enabled())
        return denied(u"the desktop integration is off (Jarvis → Control center → Desktop)"_s);
    if (!m_actionLimit.allow(caller(), now())) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::LimitsExceeded, u"too many actions; try again in a moment"_s);
        return u"rate limited"_s;
    }
    const QString target = integration::actionTarget(id);
    if (!target.isEmpty()) {
        m_openGui(target, {});
        return {};
    }
    if (id == u"pause-awareness")
        return m_assistant->pauseAwareness(60);
    if (id == u"resume-awareness")
        return m_assistant->pauseAwareness(0);
    m_assistant->cancel();
    return {};
}

QString IntegrationService::activeApplication()
{
    const ConfigData &cfg = m_assistant->config();
    if (!cfg.desktopIntegration)
        return denied(u"the desktop integration is off"_s);
    if (!cfg.shareActiveApp)
        return denied(u"sharing the active application is off (Jarvis → Control center → Desktop)"_s);
    if (!cfg.awarenessWindow || cfg.awarenessLevel == u"off")
        return denied(u"window awareness is off (Jarvis → Control center → Awareness)"_s);
    const QJsonObject ctx = QJsonDocument::fromJson(m_assistant->contextJson().toUtf8()).object();
    if (!ctx.value(u"available"_s).toBool())
        return compact(QJsonObject{{u"available"_s, false}});
    // An excluded or private window is not named at all; a title is never shared.
    if (ctx.value(u"excluded"_s).toBool() || ctx.value(u"private"_s).toBool())
        return compact(QJsonObject{{u"available"_s, true}, {u"private"_s, true}});
    return compact(QJsonObject{{u"available"_s, true},
                               {u"app"_s, ctx.value(u"app"_s)},
                               {u"desktopId"_s, ctx.value(u"desktopId"_s)},
                               {u"category"_s, ctx.value(u"category"_s)},
                               {u"fullscreen"_s, ctx.value(u"fullscreen"_s)}});
}

QString IntegrationService::notify(const QString &title, const QString &body)
{
    if (!enabled())
        return denied(u"the desktop integration is off"_s);
    const QString t = title.simplified(), b = body.trimmed();
    if (t.isEmpty() || t.size() > 120 || b.size() > 500) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::InvalidArgs, u"a title of 1–120 characters and a body up to 500"_s);
        return u"invalid"_s;
    }
    const qint64 at = now();
    if (!m_notifyLimit.allow(caller(), at) || !m_notifyAll.allow(u"*"_s, at)) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::LimitsExceeded, u"too many notifications"_s);
        return u"rate limited"_s;
    }
    // One key: a chatty client replaces its notification instead of piling them up.
    m_notifier->show(u"integration"_s, t, b, {}, -1);
    notePosted(u"external"_s, t);
    return {};
}

RunnerMatches IntegrationService::match(const QString &query)
{
    if (!enabled())
        return {};
    const QString question = integration::runnerQuestion(query);
    if (question.isEmpty())
        return {};
    const Lang lang = resolveLanguage(m_assistant->config().language);
    RunnerMatch m;
    m.id = u"ask:"_s + question;
    m.text = pick(lang, u"Спросить Jarvis: %1"_s, u"Ask Jarvis: %1"_s).arg(question);
    m.icon = u"org.jarvis.Jarvis"_s;
    m.properties.insert(u"subtext"_s, pick(lang, u"Ответ появится в оверлее Jarvis"_s, u"The answer appears in the Jarvis overlay"_s));
    m.properties.insert(u"actions"_s, QStringList{u"task"_s});
    return {m};
}

RunnerActions IntegrationService::runnerActions() const
{
    const Lang lang = resolveLanguage(m_assistant->config().language);
    return {RunnerAction{u"task"_s, pick(lang, u"Добавить как задачу или напоминание"_s, u"Add as a task or reminder"_s),
                         u"view-task-add"_s}};
}

void IntegrationService::run(const QString &matchId, const QString &actionId)
{
    if (!enabled() || !matchId.startsWith(u"ask:"_s) || matchId.size() > 504 || !m_actionLimit.allow(u"krunner"_s, now()))
        return;
    const QString question = matchId.mid(4).simplified();
    if (question.isEmpty())
        return;
    if (actionId == u"task") {
        const QString confirmation = m_assistant->addTask(question);
        m_notifier->show(u"krunner-task"_s, u"Jarvis"_s, confirmation, {}, -1);
        return;
    }
    // Asked in the overlay, like an accepted offer of help.
    m_openGui(u"assist"_s, question);
}

IntegrationAdaptor::IntegrationAdaptor(IntegrationService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
    setAutoRelaySignals(false);
    connect(service, &IntegrationService::statusChanged, this, &IntegrationAdaptor::StatusChanged);
    connect(service, &IntegrationService::noticePosted, this, &IntegrationAdaptor::NoticePosted);
}

QString IntegrationAdaptor::Status() { return m_service->statusJson(); }
QString IntegrationAdaptor::Actions()
{
    return compact(QJsonObject{{u"actions"_s, integration::actions(resolveLanguage(Config::loadFileOnly().language))}});
}
QString IntegrationAdaptor::SystemMetrics() { return m_service->metricsJson(); }
QString IntegrationAdaptor::RunAction(const QString &id) { return m_service->runAction(id.left(40)); }
QString IntegrationAdaptor::OpenOverlay() { return m_service->runAction(u"overlay"_s); }
QString IntegrationAdaptor::Notify(const QString &title, const QString &body) { return m_service->notify(title.left(400), body.left(2000)); }
QString IntegrationAdaptor::ActiveApplication() { return m_service->activeApplication(); }

RunnerAdaptor::RunnerAdaptor(QObject *object, IntegrationService *service)
    : QDBusAbstractAdaptor(object)
    , m_service(service)
{
}

RunnerActions RunnerAdaptor::Actions() { return m_service->runnerActions(); }
RunnerMatches RunnerAdaptor::Match(const QString &query) { return m_service->match(query.left(600)); }
void RunnerAdaptor::Run(const QString &matchId, const QString &actionId) { m_service->run(matchId, actionId.left(40)); }
QVariantMap RunnerAdaptor::Config()
{
    // KRunner asks only for queries that start with a trigger word.
    return {{u"TriggerWords"_s, integration::runnerTriggers()}, {u"MinLetterCount"_s, 8}};
}
void RunnerAdaptor::Teardown() {}

} // namespace jarvis
