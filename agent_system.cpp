#include "agent_system.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <cmath>

#include "workspace.h"

using namespace Qt::StringLiterals;

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ObjectMap)

namespace jarvis::agent {
namespace {

constexpr int kDBusTimeoutMs = 4000;
const QString kBluez = u"org.bluez"_s;
const QString kAudioSinkUuid = u"0000110b-0000-1000-8000-00805f9b34fb"_s;
const QString kHeadsetUuids = u"00001108 0000111e"_s; // HSP, HFP

void registerTypes()
{
    static const bool done = [] {
        qDBusRegisterMetaType<InterfaceMap>();
        qDBusRegisterMetaType<ObjectMap>();
        return true;
    }();
    Q_UNUSED(done);
}

QVariant property(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface,
                  const QString &name, QString *error = nullptr)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    m << iface << name;
    const QDBusMessage r = bus.call(m, QDBus::Block, kDBusTimeoutMs);
    if (r.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = r.errorMessage().isEmpty() ? r.errorName() : r.errorMessage();
        return {};
    }
    return r.arguments().value(0).value<QDBusVariant>().variant();
}

QString setProperty(const QDBusConnection &bus, const QString &service, const QString &path, const QString &iface,
                    const QString &name, const QVariant &value, QString *errorName = nullptr)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, u"org.freedesktop.DBus.Properties"_s, u"Set"_s);
    m << iface << name << QVariant::fromValue(QDBusVariant(value));
    const QDBusMessage r = bus.call(m, QDBus::Block, kDBusTimeoutMs);
    if (r.type() != QDBusMessage::ErrorMessage)
        return {};
    if (errorName)
        *errorName = r.errorName();
    return r.errorMessage().isEmpty() ? r.errorName() : r.errorMessage();
}

QString callError(const QDBusMessage &r)
{
    if (r.type() != QDBusMessage::ErrorMessage)
        return {};
    return r.errorMessage().isEmpty() ? r.errorName() : r.errorMessage();
}

bool serviceOn(const QDBusConnection &bus, const QString &name)
{
    return bus.interface() && bus.interface()->isServiceRegistered(name).value();
}

// "46%" → 46.
int percentOf(const QJsonValue &v)
{
    QString s = v.toString();
    s.remove(u'%');
    bool ok = false;
    const int p = s.trimmed().toInt(&ok);
    return ok ? p : -1;
}

QString cleanName(QString s)
{
    // Device names are data: no control characters, no line breaks, bounded.
    static const QRegularExpression control(u"[\\x00-\\x1f\\x7f]"_s);
    s.replace(control, u" "_s);
    return s.simplified().left(80);
}

} // namespace

int runProgram(const QString &program, const QStringList &args, int timeoutMs, QByteArray *out, QByteArray *err)
{
    if (QStandardPaths::findExecutable(program).isEmpty() && !program.startsWith(u'/'))
        return -1;
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setStandardInputFile(QProcess::nullDevice());
    p.start();
    if (!p.waitForStarted(2000))
        return -1;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return -1;
    }
    if (out)
        *out = p.readAllStandardOutput();
    if (err)
        *err = p.readAllStandardError();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

// ---------------------------------------------------------------------------
// Sound: pactl talks to PipeWire's PulseAudio server (or PulseAudio itself).

QList<AudioDevice> LinuxSystem::parseDevices(const QByteArray &json, const QString &defaultName)
{
    QList<AudioDevice> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        const QJsonObject props = o.value(u"properties"_s).toObject();
        AudioDevice d;
        d.name = o.value(u"name"_s).toString();
        if (d.name.endsWith(u".monitor"_s) || props.value(u"device.class"_s).toString() == u"monitor")
            continue;
        d.description = cleanName(o.value(u"description"_s).toString());
        d.isDefault = d.name == defaultName;
        d.muted = o.value(u"mute"_s).toBool();
        const QJsonObject volume = o.value(u"volume"_s).toObject();
        for (auto it = volume.begin(); it != volume.end(); ++it)
            d.volume = std::max(d.volume, percentOf(it.value().toObject().value(u"value_percent"_s)));
        d.bus = props.value(u"device.bus"_s).toString();
        d.formFactor = props.value(u"device.form_factor"_s).toString();
        d.btAddress = props.value(u"api.bluez5.address"_s).toString();
        d.activePort = o.value(u"active_port"_s).toString();
        const QString icon = props.value(u"device.icon_name"_s).toString();
        const QString port = d.activePort.toLower() + u' ' + props.value(u"device.profile.name"_s).toString().toLower()
                             + u' ' + d.name.toLower();
        d.headphones = d.formFactor == u"headphone" || d.formFactor == u"headset" || d.formFactor == u"hands-free"
                       || icon.contains(u"headphone"_s) || icon.contains(u"headset"_s) || port.contains(u"headphone"_s)
                       || port.contains(u"headset"_s);
        d.builtIn = d.bus == u"pci" && !d.headphones
                    && (d.formFactor == u"internal" || port.contains(u"speaker"_s) || port.contains(u"mic"_s)
                        || port.contains(u"analog"_s) || port.contains(u"hifi"_s));
        out.append(d);
    }
    return out;
}

QList<AudioDevice> LinuxSystem::devicesOf(const char *kind, const char *defaultCommand, QString *error)
{
    QByteArray json, name, err;
    if (runProgram(u"pactl"_s, {u"-f"_s, u"json"_s, u"list"_s, QString::fromLatin1(kind)}, 4000, &json, &err) != 0) {
        if (error)
            *error = QStandardPaths::findExecutable(u"pactl"_s).isEmpty()
                         ? u"pactl is not installed (package pulseaudio-utils)"_s
                         : u"the sound server does not answer: "_s + QString::fromUtf8(err).trimmed().left(200);
        return {};
    }
    runProgram(u"pactl"_s, {QString::fromLatin1(defaultCommand)}, 3000, &name);
    return parseDevices(json, QString::fromUtf8(name).trimmed());
}

QList<AudioDevice> LinuxSystem::outputs(QString *error) { return devicesOf("sinks", "get-default-sink", error); }
QList<AudioDevice> LinuxSystem::inputs(QString *error) { return devicesOf("sources", "get-default-source", error); }

QString LinuxSystem::setDefaultOutput(const QString &name)
{
    QByteArray err;
    if (runProgram(u"pactl"_s, {u"set-default-sink"_s, name}, 4000, nullptr, &err) != 0)
        return u"pactl set-default-sink: "_s + QString::fromUtf8(err).trimmed().left(200);
    // Streams follow the default unless an application pinned them; those are moved too.
    QThread::msleep(400);
    QByteArray json;
    if (runProgram(u"pactl"_s, {u"-f"_s, u"json"_s, u"list"_s, u"sink-inputs"_s}, 4000, &json) == 0) {
        QByteArray sinks;
        runProgram(u"pactl"_s, {u"-f"_s, u"json"_s, u"list"_s, u"short"_s, u"sinks"_s}, 3000, &sinks);
        QString index;
        for (const QJsonValue &s : QJsonDocument::fromJson(sinks).array())
            if (s.toObject().value(u"name"_s).toString() == name)
                index = QString::number(s.toObject().value(u"index"_s).toInt());
        for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
            const QJsonObject o = v.toObject();
            if (!index.isEmpty() && QString::number(o.value(u"sink"_s).toInt()) != index)
                runProgram(u"pactl"_s, {u"move-sink-input"_s, QString::number(o.value(u"index"_s).toInt()), name}, 3000);
        }
    }
    return {};
}

QString LinuxSystem::setDefaultInput(const QString &name)
{
    QByteArray err;
    if (runProgram(u"pactl"_s, {u"set-default-source"_s, name}, 4000, nullptr, &err) != 0)
        return u"pactl set-default-source: "_s + QString::fromUtf8(err).trimmed().left(200);
    return {};
}

QString LinuxSystem::setVolume(const QString &sink, int percent)
{
    QByteArray err;
    if (runProgram(u"pactl"_s, {u"set-sink-volume"_s, sink, QString::number(std::clamp(percent, 0, 150)) + u'%'}, 4000,
                   nullptr, &err)
        != 0)
        return u"pactl set-sink-volume: "_s + QString::fromUtf8(err).trimmed().left(200);
    return {};
}

QString LinuxSystem::setMute(const QString &sink, bool mute)
{
    QByteArray err;
    if (runProgram(u"pactl"_s, {u"set-sink-mute"_s, sink, mute ? u"1"_s : u"0"_s}, 4000, nullptr, &err) != 0)
        return u"pactl set-sink-mute: "_s + QString::fromUtf8(err).trimmed().left(200);
    return {};
}

// ---------------------------------------------------------------------------
// Bluetooth: BlueZ on the system bus.

BtAdapter LinuxSystem::adapter(QString *error)
{
    registerTypes();
    BtAdapter a;
    const QDBusConnection bus = QDBusConnection::systemBus();
    if (!serviceOn(bus, kBluez)) {
        if (error)
            *error = u"the Bluetooth service (bluetoothd) is not running"_s;
        return a;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(kBluez, u"/"_s, u"org.freedesktop.DBus.ObjectManager"_s,
                                                    u"GetManagedObjects"_s);
    const QDBusMessage r = bus.call(m, QDBus::Block, kDBusTimeoutMs);
    if (r.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = callError(r);
        return a;
    }
    const ObjectMap objects = qdbus_cast<ObjectMap>(r.arguments().value(0));
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (!it.value().contains(u"org.bluez.Adapter1"_s))
            continue;
        const QVariantMap p = it.value().value(u"org.bluez.Adapter1"_s);
        a.present = true;
        a.path = it.key().path();
        a.name = cleanName(p.value(u"Alias"_s).toString());
        a.powered = p.value(u"Powered"_s).toBool();
        break;
    }
    if (!a.present && error)
        *error = u"no Bluetooth adapter found"_s;
    return a;
}

QString LinuxSystem::setAdapterPowered(bool on)
{
    QString error;
    const BtAdapter a = adapter(&error);
    if (!a.present)
        return error;
    QString errorName;
    QString failed = setProperty(QDBusConnection::systemBus(), kBluez, a.path, u"org.bluez.Adapter1"_s, u"Powered"_s, on,
                                 &errorName);
    if (failed.isEmpty() || !on)
        return failed;
    // Switched off by a "soft" block (the Plasma applet, a key): the session may lift it.
    if (errorName.contains(u"Blocked"_s) || errorName.contains(u"NotReady"_s) || failed.contains(u"rfkill"_s, Qt::CaseInsensitive)
        || errorName.contains(u"Failed"_s)) {
        const QString rfkill = QStandardPaths::findExecutable(u"rfkill"_s, {u"/usr/sbin"_s, u"/sbin"_s, u"/usr/bin"_s});
        if (!rfkill.isEmpty() && runProgram(rfkill, {u"unblock"_s, u"bluetooth"_s}, 3000) == 0) {
            QThread::msleep(800);
            failed = setProperty(QDBusConnection::systemBus(), kBluez, a.path, u"org.bluez.Adapter1"_s, u"Powered"_s, true);
        }
    }
    return failed;
}

QList<BtDevice> LinuxSystem::devices(QString *error)
{
    registerTypes();
    QList<BtDevice> out;
    const QDBusConnection bus = QDBusConnection::systemBus();
    if (!serviceOn(bus, kBluez)) {
        if (error)
            *error = u"the Bluetooth service (bluetoothd) is not running"_s;
        return out;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(kBluez, u"/"_s, u"org.freedesktop.DBus.ObjectManager"_s,
                                                    u"GetManagedObjects"_s);
    const QDBusMessage r = bus.call(m, QDBus::Block, kDBusTimeoutMs);
    if (r.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = callError(r);
        return out;
    }
    const ObjectMap objects = qdbus_cast<ObjectMap>(r.arguments().value(0));
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (!it.value().contains(u"org.bluez.Device1"_s))
            continue;
        const QVariantMap p = it.value().value(u"org.bluez.Device1"_s);
        BtDevice d;
        d.path = it.key().path();
        d.address = p.value(u"Address"_s).toString();
        d.name = cleanName(p.value(u"Alias"_s).toString());
        if (d.name.isEmpty())
            d.name = cleanName(p.value(u"Name"_s).toString());
        d.icon = p.value(u"Icon"_s).toString();
        d.paired = p.value(u"Paired"_s).toBool() || p.value(u"Bonded"_s).toBool();
        d.trusted = p.value(u"Trusted"_s).toBool();
        d.connected = p.value(u"Connected"_s).toBool();
        const QStringList uuids = p.value(u"UUIDs"_s).toStringList();
        bool headset = false;
        for (const QString &u : uuids)
            headset = headset || kHeadsetUuids.contains(u.left(8));
        d.audio = uuids.contains(kAudioSinkUuid) || headset || d.icon.startsWith(u"audio-"_s);
        out.append(d);
    }
    return out;
}

QString LinuxSystem::connectDevice(const QString &path, const std::atomic_bool &cancel, int timeoutMs)
{
    const QDBusConnection bus = QDBusConnection::systemBus();
    const QDBusMessage m = QDBusMessage::createMethodCall(kBluez, path, u"org.bluez.Device1"_s, u"Connect"_s);
    QDBusPendingCall call = bus.asyncCall(m, timeoutMs);
    QElapsedTimer t;
    t.start();
    while (!call.isFinished()) {
        if (cancel) {
            // BlueZ aborts a connection attempt when asked to disconnect.
            bus.call(QDBusMessage::createMethodCall(kBluez, path, u"org.bluez.Device1"_s, u"Disconnect"_s), QDBus::Block,
                     kDBusTimeoutMs);
            return u"cancelled"_s;
        }
        if (t.elapsed() > timeoutMs + 1000)
            return u"timeout"_s;
        QThread::msleep(100);
    }
    const QDBusMessage r = call.reply();
    if (r.type() == QDBusMessage::ErrorMessage) {
        // "Already connected" is the state we want.
        if (r.errorName().endsWith(u"AlreadyConnected"_s))
            return {};
        return r.errorName() + u": "_s + r.errorMessage().left(200);
    }
    return {};
}

QString LinuxSystem::disconnectDevice(const QString &path)
{
    const QDBusMessage r = QDBusConnection::systemBus().call(
        QDBusMessage::createMethodCall(kBluez, path, u"org.bluez.Device1"_s, u"Disconnect"_s), QDBus::Block, 10000);
    return callError(r);
}

// ---------------------------------------------------------------------------
// Screen brightness (KDE's org.kde.ScreenBrightness, Plasma 6) and notifications.

namespace {
const QString kBrightness = u"org.kde.ScreenBrightness"_s;

QString brightnessDisplay(const QDBusConnection &bus, QString *error)
{
    if (!serviceOn(bus, kBrightness)) {
        if (error)
            *error = u"no brightness service (KDE PowerDevil, Plasma 6)"_s;
        return {};
    }
    const QStringList displays
        = property(bus, kBrightness, u"/org/kde/ScreenBrightness"_s, kBrightness, u"DisplaysDBusNames"_s, error).toStringList();
    for (const QString &d : displays)
        if (property(bus, kBrightness, u"/org/kde/ScreenBrightness/"_s + d, u"org.kde.ScreenBrightness.Display"_s,
                     u"IsInternal"_s)
                .toBool())
            return d;
    if (displays.isEmpty() && error)
        *error = u"no screen with adjustable brightness"_s;
    return displays.value(0);
}
} // namespace

int LinuxSystem::brightness(QString *error)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    const QString d = brightnessDisplay(bus, error);
    if (d.isEmpty())
        return -1;
    const QString path = u"/org/kde/ScreenBrightness/"_s + d;
    const QString iface = u"org.kde.ScreenBrightness.Display"_s;
    const int value = property(bus, kBrightness, path, iface, u"Brightness"_s, error).toInt();
    const int max = property(bus, kBrightness, path, iface, u"MaxBrightness"_s, error).toInt();
    if (max <= 0)
        return -1;
    return int(std::lround(100.0 * value / max));
}

QString LinuxSystem::setBrightness(int percent)
{
    QString error;
    const QDBusConnection bus = QDBusConnection::sessionBus();
    const QString d = brightnessDisplay(bus, &error);
    if (d.isEmpty())
        return error;
    const QString path = u"/org/kde/ScreenBrightness/"_s + d;
    const int max = property(bus, kBrightness, path, u"org.kde.ScreenBrightness.Display"_s, u"MaxBrightness"_s, &error).toInt();
    if (max <= 0)
        return error.isEmpty() ? u"the screen reports no brightness range"_s : error;
    QDBusMessage m = QDBusMessage::createMethodCall(kBrightness, path, u"org.kde.ScreenBrightness.Display"_s, u"SetBrightness"_s);
    m << int(std::lround(max * std::clamp(percent, 1, 100) / 100.0)) << uint(0);
    return callError(bus.call(m, QDBus::Block, kDBusTimeoutMs));
}

int LinuxSystem::doNotDisturb(QString *error)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = u"org.freedesktop.Notifications"_s;
    if (!serviceOn(bus, service)) {
        if (error)
            *error = u"no notification service"_s;
        return -1;
    }
    QString failed;
    const QVariant v = property(bus, service, u"/org/freedesktop/Notifications"_s, service, u"Inhibited"_s, &failed);
    if (!v.isValid()) {
        if (error)
            *error = u"the notification service has no \"do not disturb\" (Inhibited): "_s + failed;
        return -1;
    }
    return v.toBool() ? 1 : 0;
}

QString LinuxSystem::setDoNotDisturb(bool on)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = u"org.freedesktop.Notifications"_s;
    if (on) {
        if (m_dndCookie)
            return {};
        QDBusMessage m = QDBusMessage::createMethodCall(service, u"/org/freedesktop/Notifications"_s, service, u"Inhibit"_s);
        m << u"org.jarvis.Jarvis"_s << u"Jarvis"_s << QVariantMap{};
        const QDBusMessage r = bus.call(m, QDBus::Block, kDBusTimeoutMs);
        if (r.type() == QDBusMessage::ErrorMessage)
            return callError(r);
        m_dndCookie = r.arguments().value(0).toUInt();
        return {};
    }
    if (m_dndCookie) {
        QDBusMessage m = QDBusMessage::createMethodCall(service, u"/org/freedesktop/Notifications"_s, service, u"UnInhibit"_s);
        m << m_dndCookie;
        const QString error = callError(bus.call(m, QDBus::Block, kDBusTimeoutMs));
        if (!error.isEmpty())
            return error;
        m_dndCookie = 0;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Power and network.

Battery LinuxSystem::battery()
{
    Battery b;
    const QDBusConnection bus = QDBusConnection::systemBus();
    const QString service = u"org.freedesktop.UPower"_s;
    const QString path = u"/org/freedesktop/UPower/devices/DisplayDevice"_s;
    const QString iface = u"org.freedesktop.UPower.Device"_s;
    if (!serviceOn(bus, service))
        return b;
    b.present = property(bus, service, path, iface, u"IsPresent"_s).toBool();
    if (!b.present)
        return b;
    b.percent = property(bus, service, path, iface, u"Percentage"_s).toDouble();
    static const QStringList states{u"unknown"_s, u"charging"_s, u"discharging"_s, u"empty"_s, u"full"_s, u"pending-charge"_s,
                                    u"pending-discharge"_s};
    b.state = states.value(property(bus, service, path, iface, u"State"_s).toInt(), u"unknown"_s);
    b.secondsToEmpty = property(bus, service, path, iface, u"TimeToEmpty"_s).toLongLong();
    b.secondsToFull = property(bus, service, path, iface, u"TimeToFull"_s).toLongLong();
    return b;
}

namespace {
std::pair<QString, QString> powerService(const QDBusConnection &bus)
{
    if (serviceOn(bus, u"org.freedesktop.UPower.PowerProfiles"_s))
        return {u"org.freedesktop.UPower.PowerProfiles"_s, u"/org/freedesktop/UPower/PowerProfiles"_s};
    if (serviceOn(bus, u"net.hadess.PowerProfiles"_s))
        return {u"net.hadess.PowerProfiles"_s, u"/net/hadess/PowerProfiles"_s};
    return {};
}
} // namespace

QString LinuxSystem::powerProfile(QString *error)
{
    const QDBusConnection bus = QDBusConnection::systemBus();
    const auto [service, path] = powerService(bus);
    if (service.isEmpty()) {
        if (error)
            *error = u"power-profiles-daemon is not running"_s;
        return {};
    }
    return property(bus, service, path, service, u"ActiveProfile"_s, error).toString();
}

QString LinuxSystem::setPowerProfile(const QString &profile)
{
    const QDBusConnection bus = QDBusConnection::systemBus();
    const auto [service, path] = powerService(bus);
    if (service.isEmpty())
        return u"power-profiles-daemon is not running"_s;
    // polkit decides: the active local session may switch profiles without a password.
    return setProperty(bus, service, path, service, u"ActiveProfile"_s, profile);
}

QString LinuxSystem::network(QString *detail)
{
    const QDBusConnection bus = QDBusConnection::systemBus();
    const QString service = u"org.freedesktop.NetworkManager"_s;
    const QString path = u"/org/freedesktop/NetworkManager"_s;
    if (!serviceOn(bus, service))
        return u"unknown"_s;
    const uint connectivity = property(bus, service, path, service, u"Connectivity"_s).toUInt();
    const QString primary = property(bus, service, path, service, u"PrimaryConnection"_s).value<QDBusObjectPath>().path();
    if (detail && primary.size() > 1)
        *detail = cleanName(property(bus, service, primary, u"org.freedesktop.NetworkManager.Connection.Active"_s, u"Id"_s).toString());
    switch (connectivity) {
    case 4: return u"full"_s;
    case 3: return u"limited"_s;
    case 2: return u"portal"_s;
    case 1: return u"none"_s;
    default: break;
    }
    const uint state = property(bus, service, path, service, u"State"_s).toUInt();
    return state >= 70 ? u"full"_s : state >= 50 ? u"limited"_s : state > 0 ? u"none"_s : u"unknown"_s;
}

// ---------------------------------------------------------------------------
// Applications, windows, the desktop.

QList<AppInfo> LinuxSystem::findApps(const QString &query)
{
    QList<AppInfo> out;
    for (const workspace::App &a : workspace::findApplications(query, 6))
        out.append({a.desktopId, cleanName(a.name), a.score});
    return out;
}

QString LinuxSystem::defaultApp(const QString &kind)
{
    static const QMap<QString, QString> mime{{u"browser"_s, u"x-scheme-handler/https"_s},
                                             {u"player"_s, u"audio/mpeg"_s},
                                             {u"video"_s, u"video/mp4"_s},
                                             {u"files"_s, u"inode/directory"_s},
                                             {u"mail"_s, u"x-scheme-handler/mailto"_s},
                                             {u"editor"_s, u"text/plain"_s},
                                             {u"images"_s, u"image/png"_s},
                                             {u"pdf"_s, u"application/pdf"_s}};
    // No MIME type names these: the usual ones, KDE's first.
    static const QMap<QString, QStringList> known{
        {u"terminal"_s, {u"org.kde.konsole"_s, u"org.gnome.Terminal"_s, u"org.gnome.Ptyxis"_s, u"xterm"_s}},
        {u"calculator"_s, {u"org.kde.kcalc"_s, u"org.kde.kalk"_s, u"org.gnome.Calculator"_s, u"gnome-calculator"_s, u"galculator"_s}}};
    if (known.contains(kind)) {
        for (const QString &id : known.value(kind))
            if (!workspace::resolveApp(id).isEmpty())
                return id;
        return {};
    }
    if (!mime.contains(kind))
        return {};
    QByteArray out;
    if (runProgram(u"xdg-mime"_s, {u"query"_s, u"default"_s, mime.value(kind)}, 3000, &out) != 0)
        return {};
    QString id = QString::fromUtf8(out).trimmed().section(u';', 0, 0);
    if (id.endsWith(u".desktop"_s))
        id.chop(8);
    return workspace::resolveApp(id);
}

QString LinuxSystem::launch(const QString &desktopId)
{
    if (m_hooks.launch)
        return m_hooks.launch(desktopId);
    if (workspace::resolveApp(desktopId).isEmpty())
        return u"not installed: "_s + desktopId;
    for (const QString &tool : {u"kstart"_s, u"kstart5"_s})
        if (!QStandardPaths::findExecutable(tool).isEmpty() && QProcess::startDetached(tool, {u"--application"_s, desktopId}))
            return {};
    if (QProcess::startDetached(u"gtk-launch"_s, {desktopId}))
        return {};
    return u"no launcher (kstart) to start applications with"_s;
}

bool LinuxSystem::windowsAvailable() { return bool(m_hooks.windows); }

QList<Window> LinuxSystem::windows(QString *error)
{
    if (m_hooks.windows)
        return m_hooks.windows(error);
    if (error)
        *error = u"windows are managed through the Jarvis service (jarvisd), which is not running"_s;
    return {};
}

QString LinuxSystem::activateWindow(const QString &id)
{
    return m_hooks.window ? m_hooks.window(u"activate"_s, id) : u"no window manager connection"_s;
}

QString LinuxSystem::closeWindow(const QString &id)
{
    return m_hooks.window ? m_hooks.window(u"close"_s, id) : u"no window manager connection"_s;
}

QString LinuxSystem::maximizeWindow(const QString &id)
{
    return m_hooks.window ? m_hooks.window(u"maximize"_s, id) : u"no window manager connection"_s;
}

QString LinuxSystem::openSettings(const QString &key)
{
    if (m_hooks.openSettings)
        return m_hooks.openSettings(key);
    const QString module = workspace::settingsModule(key);
    if (module.isEmpty())
        return u"unknown settings page"_s;
    if (QProcess::startDetached(u"systemsettings"_s, {module}) || QProcess::startDetached(u"kcmshell6"_s, {module}))
        return {};
    return u"System Settings is not installed"_s;
}

QString LinuxSystem::openBluetoothWizard()
{
    // KDE's own pairing assistant asks for the PIN or the confirmation itself.
    if (!QStandardPaths::findExecutable(u"bluedevil-wizard"_s).isEmpty() && QProcess::startDetached(u"bluedevil-wizard"_s, {}))
        return {};
    return openSettings(u"bluetooth"_s);
}

QString LinuxSystem::switchProfile(const QString &id)
{
    return m_hooks.switchProfile ? m_hooks.switchProfile(id)
                                 : u"workspaces are switched by the Jarvis service (jarvisd), which is not running"_s;
}

QString LinuxSystem::currentProfile() { return m_hooks.currentProfile ? m_hooks.currentProfile() : QString(); }

QString LinuxSystem::media(const QString &action)
{
    if (m_hooks.media)
        return m_hooks.media(action);
    // MPRIS: the first player that plays (or any player).
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface())
        return u"no session bus"_s;
    QStringList players;
    for (const QString &name : bus.interface()->registeredServiceNames().value())
        if (name.startsWith(u"org.mpris.MediaPlayer2."_s))
            players.append(name);
    if (players.isEmpty())
        return u"no media player is running"_s;
    QString target = players.first();
    for (const QString &p : players)
        if (property(bus, p, u"/org/mpris/MediaPlayer2"_s, u"org.mpris.MediaPlayer2.Player"_s, u"PlaybackStatus"_s).toString()
            == u"Playing")
            target = p;
    static const QMap<QString, QString> methods{{u"play-pause"_s, u"PlayPause"_s}, {u"play"_s, u"Play"_s},
                                                {u"pause"_s, u"Pause"_s},          {u"next"_s, u"Next"_s},
                                                {u"previous"_s, u"Previous"_s},    {u"stop"_s, u"Stop"_s}};
    if (!methods.contains(action))
        return u"unknown media action"_s;
    return callError(bus.call(QDBusMessage::createMethodCall(target, u"/org/mpris/MediaPlayer2"_s,
                                                             u"org.mpris.MediaPlayer2.Player"_s, methods.value(action)),
                              QDBus::Block, kDBusTimeoutMs));
}

QList<std::pair<QString, QString>> LinuxSystem::capabilities()
{
    QList<std::pair<QString, QString>> out;
    QString e;
    outputs(&e);
    out.append({u"audio"_s, e});
    e.clear();
    adapter(&e);
    out.append({u"bluetooth"_s, e});
    e.clear();
    brightness(&e);
    out.append({u"brightness"_s, e});
    e.clear();
    doNotDisturb(&e);
    out.append({u"dnd"_s, e});
    e.clear();
    windows(&e);
    out.append({u"windows"_s, windowsAvailable() ? e : u"no window manager connection (needs jarvisd and KWin)"_s});
    e.clear();
    powerProfile(&e);
    out.append({u"power"_s, e});
    QString detail;
    out.append({u"network"_s, network(&detail) == u"unknown" ? u"NetworkManager does not answer"_s : QString()});
    return out;
}

} // namespace jarvis::agent
