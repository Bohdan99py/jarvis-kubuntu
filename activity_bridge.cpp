#include "activity_bridge.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "assistant.h"
#include "dbus_names.h"
#include "runtime_paths.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr int kTimeoutMs = 3000;

QDBusMessage kwinCall(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/Scripting"_s,
                                                      u"org.kde.kwin.Scripting"_s, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, kTimeoutMs);
}

// One script per daemon bus name, so a development daemon never touches the
// installed daemon's script.
QString pluginName(const QString &service)
{
    return service == QLatin1String(dbus::kService) ? u"jarvis-activity"_s : u"jarvis-activity-"_s + service;
}

QString generatedScriptPath(const QString &plugin)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return dir + u"/"_s + plugin + u".js"_s;
}

} // namespace

ActivityBridge::ActivityBridge(Assistant *assistant, QString service, QObject *parent)
    : QObject(parent)
    , m_assistant(assistant)
    , m_service(std::move(service))
    , m_kwinWatcher(u"org.kde.KWin"_s, QDBusConnection::sessionBus(),
                    QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)
{
    QDBusConnection::sessionBus().connect(u"org.freedesktop.ScreenSaver"_s, u"/ScreenSaver"_s,
                                          u"org.freedesktop.ScreenSaver"_s, u"ActiveChanged"_s, this,
                                          SLOT(onScreenSaverActive(bool)));
    // At login the user service can start before KWin owns its name: load the
    // script as soon as KWin appears, and again if KWin restarts.
    connect(&m_kwinWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        m_attempts = 0;
        if (m_enabled)
            tryLoad();
    });
    connect(&m_kwinWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        m_loaded = false;
        m_retry.stop();
        if (m_enabled)
            m_assistant->setTrackingStatus(u"no-kwin"_s);
    });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &ActivityBridge::tryLoad);
}

ActivityBridge::~ActivityBridge()
{
    unload();
}

void ActivityBridge::apply(bool enabled)
{
    m_enabled = enabled;
    m_retry.stop();
    if (!enabled) {
        unload();
        m_assistant->setTrackingStatus(u"off"_s);
        return;
    }
    m_attempts = 0;
    tryLoad();
}

void ActivityBridge::tryLoad()
{
    if (!m_enabled)
        return;
    // Always reload: a script left by a previous daemon may target an old build.
    QString error;
    if (load(&error)) {
        m_assistant->setTrackingStatus(u"kwin"_s);
        return;
    }
    m_assistant->setTrackingStatus(error);
    if (error == u"no-kwin") {
        qInfo() << "activity tracking: waiting for KWin";
        return; // the watcher calls back when KWin registers
    }
    qWarning().noquote() << "activity tracking unavailable:" << error;
    // KWin may own its name a moment before /Scripting is ready.
    if (++m_attempts < 5)
        m_retry.start(1000 * m_attempts);
}

bool ActivityBridge::isKWin(const QString &sender) const
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || sender.isEmpty())
        return false;
    const QDBusReply<QString> owner = bus->serviceOwner(u"org.kde.KWin"_s);
    return owner.isValid() && owner.value() == sender;
}

bool ActivityBridge::load(QString *error)
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(u"org.kde.KWin"_s).value()) {
        *error = u"no-kwin"_s;
        return false;
    }
    static const QRegularExpression validService(u"^[A-Za-z0-9_.]{3,200}$"_s);
    if (!validService.match(m_service).hasMatch()) {
        *error = u"error:invalid service name"_s;
        return false;
    }

    QFile templ(jarvisDataFile(u"kwin/activity.js"_s));
    if (!templ.open(QIODevice::ReadOnly)) {
        *error = u"error:"_s + templ.fileName() + u": "_s + templ.errorString();
        return false;
    }
    QByteArray script = templ.readAll();
    script.replace("%SERVICE%", m_service.toUtf8());

    const QString path = generatedScriptPath(pluginName(m_service));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(script) != script.size()
        || !out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || !out.commit()) {
        *error = u"error:"_s + path + u": "_s + out.errorString();
        return false;
    }

    unload();
    const QDBusMessage loaded = kwinCall(u"loadScript"_s, {path, pluginName(m_service)});
    if (loaded.type() == QDBusMessage::ErrorMessage) {
        *error = u"error:"_s + loaded.errorMessage();
        return false;
    }
    if (loaded.arguments().value(0).toInt() < 0) {
        *error = u"error:KWin rejected the script"_s;
        return false;
    }
    m_loaded = true;
    const QDBusMessage started = kwinCall(u"start"_s);
    if (started.type() == QDBusMessage::ErrorMessage) {
        *error = u"error:"_s + started.errorMessage();
        unload();
        return false;
    }
    qInfo() << "activity tracking: KWin script loaded";
    return true;
}

void ActivityBridge::unload()
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(u"org.kde.KWin"_s).value()) {
        m_loaded = false;
        return;
    }
    // Also clears a script that a crashed daemon left behind.
    const QDBusMessage reply = kwinCall(u"isScriptLoaded"_s, {pluginName(m_service)});
    if (m_loaded || reply.arguments().value(0).toBool()) {
        kwinCall(u"unloadScript"_s, {pluginName(m_service)});
        qInfo() << "activity tracking: KWin script unloaded";
    }
    m_loaded = false;
}

void ActivityBridge::onScreenSaverActive(bool active)
{
    m_assistant->setScreenLocked(active);
}

} // namespace jarvis
