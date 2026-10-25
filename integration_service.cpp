#include "integration_service.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>

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

IntegrationService::IntegrationService(Assistant *assistant, Notifier *notifier, GuiCall gui, QObject *parent)
    : QObject(parent)
    , m_assistant(assistant)
    , m_notifier(notifier)
    , m_callGui(std::move(gui))
    , m_workspaces(assistant, &m_telemetry, m_callGui)
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

    // The wallpaper: measured at start and when it changes (checked every two minutes; a
    // changed file is read once, scaled to 96 px — a few milliseconds).
    m_wallpaper = integration::wallpaperInfo({});
    m_wallpaperCheck.setInterval(120000);
    m_wallpaperCheck.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_wallpaperCheck, &QTimer::timeout, this, &IntegrationService::checkWallpaper);
    m_wallpaperCheck.start();
    QTimer::singleShot(2000, this, &IntegrationService::checkWallpaper);

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
    connect(m_assistant, &Assistant::routed, this, [this](quint64 id, const QString &route) {
        if (id == 0) {
            pushEvent(u"background"_s, route);
            return;
        }
        if (!m_requests.contains(id)) {
            m_requests.insert(id);
            m_started.insert(id, now());
            m_routes.clear();
            ++m_requestsTotal;
        }
        if (!m_routes.contains(route))
            m_routes.append(route.left(20));
        pushEvent(u"route"_s, route);
        touch();
    });
    connect(m_assistant, &Assistant::replyReady, this, [this](quint64 id, const QString &) {
        if (m_requests.remove(id)) {
            m_latency = double(now() - m_started.take(id));
            pushEvent(u"answer"_s, m_routes.join(u" › "_s));
            touch();
        }
    });
    connect(m_assistant, &Assistant::failed, this, [this](quint64 id, const QString &) {
        m_requests.remove(id);
        m_started.remove(id);
        ++m_errorsTotal;
        pushEvent(u"error"_s, QString());
        m_errorAt = now();
        m_stateExpiry.start(int(kErrorShownMs) + 50);
        touch();
    });
    connect(m_assistant, &Assistant::notice, this, [this](const QJsonObject &notice) {
        notePosted(notice.value(u"kind"_s).toString(), notice.value(u"title"_s).toString());
    });
    connect(m_assistant, &Assistant::contextChanged, this, &IntegrationService::touch);
    connect(&m_workspaces, &WorkspaceService::changed, this, &IntegrationService::touch);
    connect(&m_workspaces, &WorkspaceService::event, this, &IntegrationService::pushEvent);
    // Chat and voice: "prepare the desktop for programming", "open the downloads folder".
    m_assistant->setDesktopHandler([this](const workspace::Command &command, Lang lang) {
        return enabled() ? m_workspaces.handle(command, lang) : QString();
    });
    // The first CPU and network figures need an earlier reading to compare with.
    m_telemetry.json({u"cpu"_s, u"net"_s});
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
    // 0.19.2, for the desktop shell: what the core shows and the status widget lists.
    o[u"model"_s] = cfg.model;
    o[u"webSearch"_s] = cfg.webSearch;
    o[u"activity"_s] = QJsonObject{{u"requests"_s, m_requestsTotal}, {u"errors"_s, m_errorsTotal},
                                   {u"routes"_s, QJsonArray::fromStringList(m_routes)}, {u"latency"_s, m_latency}};
    o[u"voice"_s] = m_voice;
    o[u"desktop"_s] = cfg.desktop;
    o[u"wallpaper"_s] = m_wallpaper;
    o[u"workspace"_s] = m_workspaces.summary();
    o[u"components"_s] = m_components;
    if (cfg.desktopIntegration) {
        // The next reminder is personal: only with the integration on (as the notice).
        const QJsonObject task = m_assistant->nextTask();
        if (!task.isEmpty())
            o[u"task"_s] = task;
        o[u"events"_s] = m_events;
    }
    return compact(o);
}

void IntegrationService::checkWallpaper()
{
    const auto read = [](const QString &name) {
        QFile f(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + name);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    const QString image = integration::wallpaperImage(read(u"plasma-org.kde.plasma.desktop-appletsrc"_s), read(u"kdeglobals"_s),
                                                      QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation));
    const QString key = image + u'@' + QString::number(QFileInfo(image).lastModified().toMSecsSinceEpoch());
    if (key == m_wallpaperKey)
        return;
    m_wallpaperKey = key;
    const auto take = [this](const integration::Luminance &l) {
        const QJsonObject info = integration::wallpaperInfo(l);
        if (info != m_wallpaper) {
            m_wallpaper = info;
            touch();
        }
    };
    if (image.isEmpty() || integration::isRasterImage(image)) {
        take(image.isEmpty() ? integration::Luminance{} : integration::imageLuminance(image));
        return;
    }
    // A vector wallpaper (Kubuntu's are SVG): measured by the Jarvis binary in a short
    // offscreen process — the SVG reader needs a GUI application, the daemon has none.
    const QString helper = QCoreApplication::applicationDirPath() + u"/jarvis"_s;
    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    p->setProcessEnvironment(env);
    connect(p, &QProcess::finished, this, [p, take](int code, QProcess::ExitStatus status) {
        const QStringList v = QString::fromLatin1(p->readAllStandardOutput()).split(u' ', Qt::SkipEmptyParts);
        integration::Luminance l;
        if (status == QProcess::NormalExit && code == 0 && v.size() == 2) {
            l.center = v[0].toDouble();
            l.whole = v[1].toDouble();
        }
        take(l);
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError) { p->deleteLater(); });
    QTimer::singleShot(15000, p, [p] { p->kill(); });
    p->start(QFileInfo::exists(helper) ? helper : u"jarvis"_s, {u"--measure-image"_s, image});
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
        return denied(u"the desktop integration is off (Jarvis → Settings → Desktop)"_s);
    if (!m_actionLimit.allow(caller(), now())) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::LimitsExceeded, u"too many actions; try again in a moment"_s);
        return u"rate limited"_s;
    }
    const QString target = integration::actionTarget(id);
    if (!target.isEmpty()) {
        m_callGui(u"Open"_s, {target, QString()});
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
        return denied(u"sharing the active application is off (Jarvis → Settings → Desktop)"_s);
    if (!cfg.awarenessWindow || cfg.awarenessLevel == u"off")
        return denied(u"window awareness is off (Jarvis → Settings → Memory and privacy)"_s);
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

void IntegrationService::pushEvent(const QString &kind, const QString &text)
{
    const QJsonObject e{{u"at"_s, double(now())}, {u"kind"_s, kind.left(20)}, {u"text"_s, text.left(120)}};
    m_events.append(e);
    while (m_events.size() > 12)
        m_events.removeFirst();
    if (enabled())
        emit eventPosted(compact(e));
}

QString IntegrationService::eventsJson() const
{
    return enabled() ? compact(QJsonObject{{u"events"_s, m_events}}) : u"{\"events\":[]}"_s;
}

bool IntegrationService::callerIsKWin() const
{
    if (!calledFromDBus())
        return false;
    const auto owner = connection().interface()->serviceOwner(u"org.kde.KWin"_s);
    return owner.isValid() && owner.value() == message().service();
}

bool IntegrationService::callerIsGui() const
{
    if (!calledFromDBus())
        return true;
    const auto owner = connection().interface()->serviceOwner(u"org.jarvis.Desktop1"_s);
    return owner.isValid() && owner.value() == message().service();
}

bool IntegrationService::acting(const QString &what)
{
    if (!enabled()) {
        denied(u"the desktop integration is off (Jarvis → Settings → Desktop)"_s);
        return false;
    }
    if (!m_actionLimit.allow(caller(), now())) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::LimitsExceeded, u"too many requests (%1); try again in a moment"_s.arg(what));
        return false;
    }
    return true;
}

QString IntegrationService::telemetry(const QString &kinds)
{
    QStringList wanted;
    for (const QString &k : kinds.split(u',', Qt::SkipEmptyParts))
        if (TelemetryService::kinds().contains(k.trimmed()) && !wanted.contains(k.trimmed()))
            wanted.append(k.trimmed());
    // Media names what the user listens to: with the integration on only.
    if (!enabled())
        wanted.removeAll(u"media"_s);
    return compact(m_telemetry.json(wanted));
}

QString IntegrationService::mediaControl(const QString &action)
{
    if (!acting(u"media"_s))
        return u"refused"_s;
    return m_telemetry.mediaControl(action.left(20));
}

QString IntegrationService::setPowerProfile(const QString &profile)
{
    if (!acting(u"power"_s))
        return u"refused"_s;
    const QString error = m_telemetry.setPowerProfile(profile.left(20));
    if (error.isEmpty())
        pushEvent(u"power"_s, profile.left(20));
    else if (calledFromDBus())
        sendErrorReply(QDBusError::InvalidArgs, error);
    return error;
}

QString IntegrationService::switchWorkspace(const QString &id)
{
    if (!acting(u"workspace"_s))
        return u"refused"_s;
    const Lang lang = resolveLanguage(m_assistant->config().language);
    QString message, error;
    if (id == u"previous")
        error = m_workspaces.previous(lang, &message);
    else if (id.startsWith(u"prepare:"_s))
        error = m_workspaces.switchTo(id.mid(8), true, lang, &message);
    else
        error = m_workspaces.switchTo(id, false, lang, &message);
    if (!error.isEmpty() && calledFromDBus())
        sendErrorReply(QDBusError::InvalidArgs, error);
    return error.isEmpty() ? QString() : error;
}

QString IntegrationService::workspaces()
{
    return compact(m_workspaces.summary());
}

QString IntegrationService::saveWorkspaceLayout()
{
    if (!acting(u"layout"_s))
        return u"refused"_s;
    return m_workspaces.saveLayout(resolveLanguage(m_assistant->config().language));
}

void IntegrationService::workspaceReport(const QString &json)
{
    // Only KWin runs the workspace script; anyone else could fake running windows.
    if (!callerIsKWin()) {
        denied(u"workspace reports are accepted from KWin only"_s);
        return;
    }
    m_workspaces.report(json);
}

void IntegrationService::reportVoice(bool listening, bool speaking, const QString &engine)
{
    if (!callerIsGui()) {
        denied(u"the voice state comes from the Jarvis window only"_s);
        return;
    }
    m_voice = QJsonObject{{u"listening"_s, listening}, {u"speaking"_s, speaking}, {u"engine"_s, engine.left(40)}, {u"at"_s, double(now())}};
    touch();
}

void IntegrationService::componentAlive(const QString &component)
{
    static const QStringList known{u"core"_s, u"dock"_s, u"system"_s, u"network"_s, u"power"_s, u"media"_s, u"status"_s};
    // Every widget in plasmashell shares one bus connection: a limit per part, not per connection.
    if (!known.contains(component) || !m_actionLimit.allow(u"alive:"_s + caller() + u':' + component, now()))
        return;
    const bool fresh = !m_components.contains(component);
    m_components[component] = double(now());
    if (fresh)
        touch(); // a part appeared; later heartbeats need no status change
}

RunnerMatches IntegrationService::match(const QString &query)
{
    if (!enabled())
        return {};
    const Lang lang = resolveLanguage(m_assistant->config().language);
    RunnerMatches out;
    const auto add = [&out](const QString &id, const QString &text, const QString &icon, const QString &subtext, double relevance, int type = 100) {
        RunnerMatch m;
        m.id = id;
        m.text = text;
        m.icon = icon;
        m.type = type;
        m.relevance = relevance;
        if (!subtext.isEmpty())
            m.properties.insert(u"subtext"_s, subtext);
        out.append(m);
    };
    // Desktop commands first: they are what the words say, without Claude.
    const workspace::Command c = workspace::parseCommand(query);
    if (c.kind == u"workspace") {
        const workspace::Profile p = workspace::find(workspace::load(), c.arg);
        add(u"ws:"_s + (c.prepare ? u"prepare:"_s : QString()) + p.id,
            pick(lang, u"Рабочее пространство %1"_s, u"Workspace %1"_s).arg(p.label),
            u"preferences-desktop-virtual"_s,
            pick(lang, u"Рабочий стол %1 и программы профиля"_s, u"Desktop %1 and the profile's applications"_s).arg(p.desktop), 1.0);
    } else if (c.kind == u"previous-workspace") {
        add(u"ws:previous"_s, pick(lang, u"Предыдущее рабочее пространство"_s, u"Previous workspace"_s), u"go-previous"_s, {}, 1.0);
    } else if (c.kind == u"open-app") {
        for (const workspace::App &app : workspace::findApplications(c.arg, 3))
            add(u"app:"_s + app.desktopId, pick(lang, u"Открыть %1"_s, u"Open %1"_s).arg(app.name), app.icon, app.desktopId, app.score);
    } else if (c.kind == u"open-folder") {
        const QString path = workspace::folderPath(c.arg);
        if (!path.isEmpty())
            add(u"dir:"_s + c.arg, pick(lang, u"Открыть папку %1"_s, u"Open the folder %1"_s).arg(QFileInfo(path).fileName()),
                u"folder"_s, path, 1.0);
    } else if (c.kind == u"find-project") {
        for (const workspace::Project &p : m_workspaces.projects(c.arg))
            add(u"proj:"_s + p.path, pick(lang, u"Проект %1 (%2)"_s, u"Project %1 (%2)"_s).arg(p.name, p.kind), u"folder-development"_s,
                p.path, 0.9);
    } else if (c.kind == u"show-metric") {
        add(u"metric:"_s + c.arg, m_telemetry.describe(c.arg), u"utilities-system-monitor"_s,
            pick(lang, u"Сейчас, по данным Jarvis"_s, u"Now, measured by Jarvis"_s), 1.0, 50);
    } else if (c.kind == u"settings") {
        add(u"kcm:"_s + c.arg, pick(lang, u"Открыть настройки: %1"_s, u"Open the settings: %1"_s).arg(c.arg), u"preferences-system"_s, {}, 0.9);
    } else if (c.kind == u"power-profile") {
        add(u"power:"_s + c.arg, pick(lang, u"Профиль питания: %1"_s, u"Power profile: %1"_s).arg(c.arg), u"battery-profile-powersave"_s, {}, 0.9);
    }
    const QString question = integration::runnerQuestion(query);
    if (!question.isEmpty()) {
        RunnerMatch m;
        m.id = u"ask:"_s + question;
        m.text = pick(lang, u"Спросить Jarvis: %1"_s, u"Ask Jarvis: %1"_s).arg(question);
        m.icon = u"org.jarvis.Jarvis"_s;
        m.relevance = out.isEmpty() ? 1.0 : 0.5;
        m.properties.insert(u"subtext"_s, pick(lang, u"Ответ появится в оверлее Jarvis"_s, u"The answer appears in the Jarvis overlay"_s));
        m.properties.insert(u"actions"_s, QStringList{u"task"_s});
        out.append(m);
    }
    return out;
}

RunnerActions IntegrationService::runnerActions() const
{
    const Lang lang = resolveLanguage(m_assistant->config().language);
    return {RunnerAction{u"task"_s, pick(lang, u"Добавить как задачу или напоминание"_s, u"Add as a task or reminder"_s),
                         u"view-task-add"_s}};
}

void IntegrationService::run(const QString &matchId, const QString &actionId)
{
    if (!enabled() || matchId.size() > 1100 || !m_actionLimit.allow(u"krunner"_s, now()))
        return;
    const Lang lang = resolveLanguage(m_assistant->config().language);
    const QString kind = matchId.section(u':', 0, 0), arg = matchId.section(u':', 1);
    if (kind == u"ws") {
        if (arg == u"previous")
            m_workspaces.previous(lang);
        else if (arg.startsWith(u"prepare:"_s))
            m_workspaces.switchTo(arg.mid(8), true, lang);
        else
            m_workspaces.switchTo(arg, false, lang);
    } else if (kind == u"app") {
        m_callGui(u"Launch"_s, {arg}); // the window checks it names an installed application
    } else if (kind == u"dir") {
        const QString path = workspace::folderPath(arg);
        if (!path.isEmpty())
            m_callGui(u"OpenPath"_s, {path});
    } else if (kind == u"proj") {
        // Only a project this service found itself, never a path someone made up.
        const QList<workspace::Project> known = m_workspaces.projects(QString());
        if (std::any_of(known.cbegin(), known.cend(), [&arg](const workspace::Project &p) { return p.path == arg; }))
            m_callGui(u"OpenPath"_s, {arg});
    } else if (kind == u"kcm") {
        if (!workspace::settingsModule(arg).isEmpty())
            m_callGui(u"OpenSettings"_s, {arg});
    } else if (kind == u"power") {
        m_telemetry.setPowerProfile(arg);
    } else if (kind == u"ask") {
        const QString question = arg.simplified().left(500);
        if (question.isEmpty())
            return;
        if (actionId == u"task") {
            const QString confirmation = m_assistant->addTask(question);
            m_notifier->show(u"krunner-task"_s, u"Jarvis"_s, confirmation, {}, -1);
            return;
        }
        // Asked in the overlay, like an accepted offer of help.
        m_callGui(u"Open"_s, {u"assist"_s, question});
    }
}

IntegrationAdaptor::IntegrationAdaptor(IntegrationService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
    setAutoRelaySignals(false);
    connect(service, &IntegrationService::statusChanged, this, &IntegrationAdaptor::StatusChanged);
    connect(service, &IntegrationService::noticePosted, this, &IntegrationAdaptor::NoticePosted);
    connect(service, &IntegrationService::eventPosted, this, &IntegrationAdaptor::EventPosted);
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
QString IntegrationAdaptor::Telemetry(const QString &kinds) { return m_service->telemetry(kinds.left(200)); }
QString IntegrationAdaptor::MediaControl(const QString &action) { return m_service->mediaControl(action); }
QString IntegrationAdaptor::SetPowerProfile(const QString &profile) { return m_service->setPowerProfile(profile); }
QString IntegrationAdaptor::Workspace(const QString &id) { return m_service->switchWorkspace(id.left(40)); }
QString IntegrationAdaptor::Workspaces() { return m_service->workspaces(); }
QString IntegrationAdaptor::SaveWorkspaceLayout() { return m_service->saveWorkspaceLayout(); }
void IntegrationAdaptor::WorkspaceReport(const QString &json) { m_service->workspaceReport(json); }
void IntegrationAdaptor::ReportVoice(bool listening, bool speaking, const QString &engine) { m_service->reportVoice(listening, speaking, engine); }
void IntegrationAdaptor::ComponentAlive(const QString &component) { m_service->componentAlive(component.left(20)); }
QString IntegrationAdaptor::Events() { return m_service->eventsJson(); }

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
    return {{u"TriggerWords"_s, integration::runnerTriggers()}, {u"MinLetterCount"_s, 4}};
}
void RunnerAdaptor::Teardown() {}

} // namespace jarvis
