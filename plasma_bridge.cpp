#include "plasma_bridge.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "build_config.h"
#include "config.h"
#include "dbus_names.h"
#include "desktop_integration.h"
#include "language.h"

using namespace Qt::StringLiterals;

namespace {

const QString kPlasmaService = u"org.kde.plasmashell"_s;
// The order rows are shown in.
const QStringList kRowOrder{u"daemon"_s, u"daemon-unit"_s, u"tray-unit"_s, u"background"_s, u"session"_s, u"widget"_s, u"runner"_s};

QDBusMessage plasmaScript(const QString &script)
{
    auto msg = QDBusMessage::createMethodCall(kPlasmaService, u"/PlasmaShell"_s, u"org.kde.PlasmaShell"_s, u"evaluateScript"_s);
    msg << script;
    return msg;
}

// The scripts print one number; Plasma returns what they printed.
int printedNumber(const QDBusMessage &reply)
{
    static const QRegularExpression number(u"^\\s*(\\d+)"_s);
    const auto m = number.match(reply.arguments().value(0).toString());
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

QVariantMap propertyMap(const QDBusMessage &reply)
{
    const QVariant v = reply.arguments().value(0);
    return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>()) : v.toMap();
}

jarvis::Lang coreLanguage()
{
    return jarvis::resolveLanguage(jarvis::Config::loadFileOnly().language);
}

} // namespace

PlasmaBridge::PlasmaBridge(QObject *parent)
    : QObject(parent)
    , m_plasmaWatcher(kPlasmaService, QDBusConnection::sessionBus(),
                      QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)
{
    connect(&m_plasmaWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        m_plasma = true;
        emit changed();
    });
    connect(&m_plasmaWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        m_plasma = false;
        m_widgets = -1;
        emit changed();
    });
    // One quick question to the bus daemon; nothing waits on Plasma itself.
    auto msg = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s, u"org.freedesktop.DBus"_s, u"NameHasOwner"_s);
    msg << kPlasmaService;
    call(msg, [this](const QDBusMessage &reply) {
        m_plasma = reply.arguments().value(0).toBool();
        emit changed();
    });
}

bool PlasmaBridge::widgetInstalled() const
{
    return !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                   u"plasma/plasmoids/"_s + jarvis::integration::kHudPlugin + u"/metadata.json"_s)
                .isEmpty();
}

bool PlasmaBridge::runnerInstalled() const
{
    return !QStandardPaths::locate(QStandardPaths::GenericDataLocation, u"krunner/dbusplugins/org.jarvis.Jarvis.desktop"_s).isEmpty();
}

void PlasmaBridge::call(const QDBusMessage &msg, const Reply &done, int timeoutMs)
{
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, timeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, done] {
        w->deleteLater();
        done(w->reply());
    });
}

void PlasmaBridge::runScript(const QString &script, const std::function<void(int, QString)> &done)
{
    ++m_busy;
    emit changed();
    call(plasmaScript(script), [this, done](const QDBusMessage &reply) {
        --m_busy;
        if (reply.type() == QDBusMessage::ErrorMessage)
            done(-1, reply.errorMessage());
        else
            done(printedNumber(reply), {});
        emit changed();
    }, 8000);
}

void PlasmaBridge::addWidget(const QString &type)
{
    if (jarvis::integration::plasmaAddScript(type).isEmpty()) {
        m_status = tr("Unknown widget.");
        emit changed();
        return;
    }
    if (!widgetInstalled()) {
        m_status = tr("The Jarvis widget is not installed (package org.jarvis.hud). Install the Jarvis package, then try again.");
        emit changed();
        return;
    }
    if (!m_plasma) {
        m_status = tr("Plasma is not running: nothing was changed.");
        emit changed();
        return;
    }
    const int before = m_widgets;
    runScript(jarvis::integration::plasmaAddScript(type), [this, before](int count, const QString &error) {
        m_widgets = count;
        m_status = !error.isEmpty() ? tr("Plasma refused: %1").arg(error)
                   : count <= 0     ? tr("Plasma did not add the widget. Are the widgets locked? Unlock them (right click on the desktop) or add \"Jarvis HUD\" yourself from \"Add Widgets…\".")
                   : before > 0     ? tr("The Jarvis widget is already on the desktop.")
                                    : tr("The Jarvis widget was added to the desktop of the first screen. Move or resize it like any widget; \"Remove\" in its menu takes it away.");
    });
}

void PlasmaBridge::removeWidgets(const QString &type)
{
    if (!m_plasma) {
        m_status = tr("Plasma is not running: nothing to remove.");
        emit changed();
        emit restored(0);
        return;
    }
    runScript(jarvis::integration::plasmaRemoveScript(type), [this](int removed, const QString &error) {
        m_widgets = error.isEmpty() ? 0 : -1;
        m_status = !error.isEmpty() ? tr("Plasma refused: %1").arg(error)
                   : removed > 0    ? tr("Removed Jarvis widgets: %1. Panels, other widgets and the wallpaper were not touched.").arg(removed)
                                    : tr("No Jarvis widgets on the desktop. Panels, other widgets and the wallpaper were not touched.");
        emit restored(qMax(0, removed));
    });
}

void PlasmaBridge::setRow(const QString &key, const QString &name, const QString &level, const QString &text)
{
    m_found.insert(key, QVariantMap{{u"name"_s, name}, {u"level"_s, level}, {u"text"_s, text}});
}

void PlasmaBridge::finishStep()
{
    if (--m_pending > 0)
        return;
    m_rows.clear();
    for (const QString &key : kRowOrder)
        if (m_found.contains(key))
            m_rows.append(m_found.value(key));
    --m_busy;
    emit changed();
    emit diagnosticsChanged();
}

void PlasmaBridge::queryUnit(const QString &key, const QString &unit, bool wanted)
{
    const QString name = tr("systemd unit %1").arg(unit);
    auto load = QDBusMessage::createMethodCall(u"org.freedesktop.systemd1"_s, u"/org/freedesktop/systemd1"_s,
                                               u"org.freedesktop.systemd1.Manager"_s, u"LoadUnit"_s);
    load << unit;
    ++m_pending;
    call(load, [this, key, unit, name, wanted](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setRow(key, name, wanted ? u"warn"_s : u"off"_s, tr("the systemd user manager is not reachable: %1").arg(reply.errorMessage()));
            finishStep();
            return;
        }
        const QString path = reply.arguments().value(0).value<QDBusObjectPath>().path();
        auto health = std::make_shared<jarvis::integration::UnitHealth>();
        health->unit = unit;
        auto getAll = [path](const QString &iface) {
            auto m = QDBusMessage::createMethodCall(u"org.freedesktop.systemd1"_s, path, u"org.freedesktop.DBus.Properties"_s, u"GetAll"_s);
            m << iface;
            return m;
        };
        call(getAll(u"org.freedesktop.systemd1.Unit"_s), [this, key, name, wanted, health, getAll](const QDBusMessage &unitReply) {
            const QVariantMap u = propertyMap(unitReply);
            health->load = u.value(u"LoadState"_s).toString();
            health->active = u.value(u"ActiveState"_s).toString();
            health->sub = u.value(u"SubState"_s).toString();
            health->fileState = u.value(u"UnitFileState"_s).toString();
            call(getAll(u"org.freedesktop.systemd1.Service"_s), [this, key, name, wanted, health](const QDBusMessage &serviceReply) {
                const QVariantMap s = propertyMap(serviceReply);
                health->result = s.value(u"Result"_s).toString();
                health->restarts = s.value(u"NRestarts"_s).toUInt();
                // systemd reports "unknown" as the largest 64-bit number.
                const quint64 memory = s.value(u"MemoryCurrent"_s).toULongLong(), cpu = s.value(u"CPUUsageNSec"_s).toULongLong();
                health->memoryBytes = memory == ~quint64(0) ? 0 : memory;
                health->cpuNs = cpu == ~quint64(0) ? 0 : cpu;
                const QJsonObject a = jarvis::integration::assessUnit(*health, wanted, coreLanguage());
                setRow(key, name, a.value(u"level"_s).toString(), a.value(u"text"_s).toString());
                finishStep();
            });
        });
    });
}

void PlasmaBridge::refresh()
{
    if (m_pending > 0)
        return;
    m_found.clear();
    ++m_busy;
    m_pending = 1; // released at the end of this function: rows are published once
    emit changed();

    // Plasma first (asked, not remembered): the session row and the widget count depend on it.
    auto plasma = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s, u"org.freedesktop.DBus"_s, u"NameHasOwner"_s);
    plasma << kPlasmaService;
    ++m_pending;
    call(plasma, [this](const QDBusMessage &reply) {
        m_plasma = reply.arguments().value(0).toBool();
        // The session.
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP"), type = qEnvironmentVariable("XDG_SESSION_TYPE");
        const QString plasmaVersion = qEnvironmentVariable("KDE_SESSION_VERSION");
        const bool kde = desktop.contains(u"KDE"_s, Qt::CaseInsensitive);
        setRow(u"session"_s, tr("Desktop session"), kde && plasmaVersion.toInt() >= 6 ? u"ok"_s : u"warn"_s,
               (kde ? tr("KDE Plasma %1").arg(plasmaVersion.isEmpty() ? u"?"_s : plasmaVersion) : (desktop.isEmpty() ? tr("unknown desktop") : desktop))
                   + u" · "_s + (type.isEmpty() ? u"?"_s : type)
                   + (m_plasma ? QString() : tr(" · plasmashell is not on the session bus"))
                   + (kde && plasmaVersion.toInt() < 6 ? tr(" · the desktop widget needs Plasma 6") : QString()));

        // The widget: installed, and how many are on the desktop.
        if (!widgetInstalled()) {
            setRow(u"widget"_s, tr("Desktop widget"), u"warn"_s, tr("not installed (plasma/plasmoids/org.jarvis.hud)"));
        } else if (!m_plasma) {
            setRow(u"widget"_s, tr("Desktop widget"), u"off"_s, tr("installed · Plasma is not running"));
        } else {
            ++m_pending;
            runScript(jarvis::integration::plasmaCountScript(), [this](int count, const QString &error) {
                m_widgets = count;
                setRow(u"widget"_s, tr("Desktop widget"), error.isEmpty() ? (count > 0 ? u"ok"_s : u"off"_s) : u"warn"_s,
                       !error.isEmpty() ? tr("installed · Plasma did not answer: %1").arg(error)
                       : count > 0      ? tr("installed · on the desktop: %1").arg(count)
                                        : tr("installed · not on the desktop"));
                finishStep();
            });
        }
        finishStep();
    });

    // The daemon: does it answer, how fast, which version.
    auto status = QDBusMessage::createMethodCall(jarvis::dbus::kService, jarvis::integration::kPath,
                                                 jarvis::integration::kInterface, u"Status"_s);
    status.setAutoStartService(false); // diagnostics look, they do not start the service
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    ++m_pending;
    call(status, [this, clock](const QDBusMessage &reply) {
        const QString name = tr("Background service jarvisd");
        if (reply.errorName() == u"org.freedesktop.DBus.Error.UnknownObject" || reply.errorName() == u"org.freedesktop.DBus.Error.UnknownMethod") {
            setRow(u"daemon"_s, name, u"warn"_s, tr("an older version is running, without the desktop integration: restart the service"));
            setRow(u"background"_s, tr("Background work"), u"off"_s, tr("unknown until the service is restarted"));
            finishStep();
            return;
        }
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setRow(u"daemon"_s, name, u"error"_s, tr("does not answer: %1").arg(reply.errorMessage()));
            setRow(u"background"_s, tr("Background work"), u"off"_s, tr("unknown while the service is not running"));
            finishStep();
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object();
        const QString version = o.value(u"version"_s).toString();
        const bool sameVersion = version == QLatin1String(JARVIS_VERSION);
        setRow(u"daemon"_s, name, sameVersion ? u"ok"_s : u"warn"_s,
               tr("answers in %1 ms · version %2").arg(clock->elapsed()).arg(version)
                   + (sameVersion ? QString() : tr(" · the window is %1: restart the service after an update").arg(QLatin1String(JARVIS_VERSION)))
                   + tr(" · desktop integration %1").arg(o.value(u"integration"_s).toBool() ? tr("on") : tr("off")));
        setRow(u"background"_s, tr("Background work"), o.value(u"background"_s).toBool() ? u"ok"_s : u"off"_s,
               o.value(u"background"_s).toBool() ? tr("on: reminders, hints, KRunner and the widget keep working after the window is closed")
                                                 : tr("off: the service ends shortly after the Jarvis window"));
        finishStep();
    }, 3000);

    queryUnit(u"daemon-unit"_s, u"jarvis.service"_s, true);
    queryUnit(u"tray-unit"_s, u"jarvis-tray.service"_s, false);

    // KRunner: the plugin file and the runner's bus name.
    auto owner = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s, u"org.freedesktop.DBus"_s, u"NameHasOwner"_s);
    owner << jarvis::dbus::runnerService();
    ++m_pending;
    call(owner, [this](const QDBusMessage &reply) {
        const bool active = reply.arguments().value(0).toBool();
        const bool installed = runnerInstalled();
        setRow(u"runner"_s, tr("KRunner"), installed && active ? u"ok"_s : installed ? u"off"_s : u"warn"_s,
               (installed ? tr("plugin installed") : tr("plugin not installed (krunner/dbusplugins/org.jarvis.Jarvis.desktop)"))
                   + (active ? tr(" · answering \"jarvis …\"") : tr(" · inactive (the service is stopped or the integration is off)")));
        finishStep();
    });

    finishStep();
}

QString PlasmaBridge::report() const
{
    QString out = u"Jarvis %1 — desktop integration\n"_s.arg(QLatin1String(JARVIS_VERSION));
    for (const QVariant &row : m_rows) {
        const QVariantMap r = row.toMap();
        out += u"[%1] %2: %3\n"_s.arg(r.value(u"level"_s).toString(), r.value(u"name"_s).toString(), r.value(u"text"_s).toString());
    }
    return out;
}

void PlasmaBridge::copyReport() const
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(report());
}

bool PlasmaBridge::disableIntegration(QString *error)
{
    jarvis::ConfigData d = jarvis::Config::loadFileOnly();
    d.desktopIntegration = false;
    d.shareActiveApp = false;
    return jarvis::Config::save(d, error);
}

int PlasmaBridge::printDiagnostics()
{
    PlasmaBridge bridge;
    QEventLoop loop;
    connect(&bridge, &PlasmaBridge::diagnosticsChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(0, &bridge, &PlasmaBridge::refresh);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    std::fputs(bridge.report().toUtf8().constData(), stdout);
    for (const QVariant &row : bridge.diagnostics())
        if (row.toMap().value(u"level"_s).toString() == u"error")
            return 1;
    return 0;
}

int PlasmaBridge::restoreFromTerminal()
{
    int status = 0;
    auto say = [](const QString &line) { std::fputs((line + u"\n"_s).toUtf8().constData(), stdout); };
    QString error;
    if (disableIntegration(&error)) {
        say(tr("Desktop integration: off (%1).").arg(jarvis::Config::filePath()));
    } else {
        say(tr("Could not change the settings: %1").arg(error));
        status = 1;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Only a running daemon is told; a stopped one reads the file when it starts.
    if (bus.interface() && bus.interface()->isServiceRegistered(jarvis::dbus::kService).value())
        bus.call(QDBusMessage::createMethodCall(jarvis::dbus::kService, jarvis::dbus::kPath, jarvis::dbus::kInterface, u"ReloadConfig"_s),
                 QDBus::Block, 3000);
    if (!bus.interface() || !bus.interface()->isServiceRegistered(kPlasmaService).value()) {
        say(tr("Plasma is not running: no widgets to remove."));
        return status;
    }
    const QDBusMessage reply = bus.call(plasmaScript(jarvis::integration::plasmaRemoveScript()), QDBus::Block, 8000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        say(tr("Plasma refused to remove the widgets: %1. Remove \"Jarvis HUD\" from the desktop yourself (right click → Remove).").arg(reply.errorMessage()));
        return 1;
    }
    say(tr("Removed Jarvis widgets: %1. Panels, other widgets and the wallpaper were not touched.").arg(qMax(0, printedNumber(reply))));
    return status;
}
