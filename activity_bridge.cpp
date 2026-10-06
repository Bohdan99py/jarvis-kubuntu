#include "activity_bridge.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "assistant.h"
#include "runtime_paths.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr auto kPlugin = "jarvis-activity";
constexpr int kTimeoutMs = 3000;

QDBusMessage kwinCall(const QString &method, const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/Scripting"_s,
                                                      u"org.kde.kwin.Scripting"_s, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, kTimeoutMs);
}

QString generatedScriptPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return dir + u"/jarvis-activity.js"_s;
}

} // namespace

ActivityBridge::ActivityBridge(Assistant *assistant, QString service, QObject *parent)
    : QObject(parent)
    , m_assistant(assistant)
    , m_service(std::move(service))
{
    QDBusConnection::sessionBus().connect(u"org.freedesktop.ScreenSaver"_s, u"/ScreenSaver"_s,
                                          u"org.freedesktop.ScreenSaver"_s, u"ActiveChanged"_s, this,
                                          SLOT(onScreenSaverActive(bool)));
}

ActivityBridge::~ActivityBridge()
{
    unload();
}

void ActivityBridge::apply(bool enabled)
{
    if (!enabled) {
        unload();
        m_assistant->setTrackingStatus(u"off"_s);
        return;
    }
    // Always reload: a script left by a previous daemon may target an old build.
    QString error;
    if (load(&error)) {
        m_assistant->setTrackingStatus(u"kwin"_s);
    } else {
        qWarning().noquote() << "activity tracking unavailable:" << error;
        m_assistant->setTrackingStatus(error);
    }
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

    const QString path = generatedScriptPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(script) != script.size()
        || !out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || !out.commit()) {
        *error = u"error:"_s + path + u": "_s + out.errorString();
        return false;
    }

    unload();
    const QDBusMessage loaded = kwinCall(u"loadScript"_s, {path, QString::fromLatin1(kPlugin)});
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
    const QDBusMessage reply = kwinCall(u"isScriptLoaded"_s, {QString::fromLatin1(kPlugin)});
    if (m_loaded || reply.arguments().value(0).toBool()) {
        kwinCall(u"unloadScript"_s, {QString::fromLatin1(kPlugin)});
        qInfo() << "activity tracking: KWin script unloaded";
    }
    m_loaded = false;
}

void ActivityBridge::onScreenSaverActive(bool active)
{
    m_assistant->setScreenLocked(active);
}

} // namespace jarvis
