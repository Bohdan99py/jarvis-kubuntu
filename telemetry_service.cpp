#include "telemetry_service.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QJsonDocument>
#include <QStandardPaths>

#include <cmath>

#include "jv_sys.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr int kHistory = 60;

QVariantMap propertyMap(const QDBusMessage &reply)
{
    const QVariant v = reply.arguments().value(0);
    return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v.value<QDBusArgument>()) : v.toMap();
}

QVariant variantValue(const QDBusMessage &reply)
{
    return reply.arguments().value(0).value<QDBusVariant>().variant();
}

QDBusMessage getProperty(const QString &service, const QString &path, const QString &iface, const QString &name)
{
    auto msg = QDBusMessage::createMethodCall(service, path, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    msg << iface << name;
    return msg;
}

void push(QJsonArray &history, double value)
{
    history.append(value);
    while (history.size() > kHistory)
        history.removeFirst();
}

double round1(double v)
{
    return std::round(v * 10) / 10;
}

// MPRIS metadata values arrive as D-Bus variants inside a map.
QString metaString(const QVariantMap &meta, const QString &key)
{
    const QVariant v = meta.value(key);
    if (v.canConvert<QDBusArgument>()) {
        const QStringList list = qdbus_cast<QStringList>(v.value<QDBusArgument>());
        return list.join(u", "_s);
    }
    if (v.typeId() == QMetaType::QStringList)
        return v.toStringList().join(u", "_s);
    return v.toString();
}

} // namespace

TelemetryService::TelemetryService(QObject *parent)
    : QObject(parent)
{
}

QStringList TelemetryService::kinds()
{
    return {u"cpu"_s, u"memory"_s, u"gpu"_s, u"thermal"_s, u"net"_s, u"power"_s, u"media"_s};
}

bool TelemetryService::stale(const QString &kind, qint64 minMs)
{
    auto it = m_age.find(kind);
    if (it != m_age.end() && it->isValid() && it->elapsed() < minMs && m_cache.contains(kind))
        return false;
    m_age[kind].start();
    return true;
}

void TelemetryService::callSystem(const QDBusMessage &msg, const Reply &done)
{
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, done] {
        w->deleteLater();
        done(w->reply());
    });
}

void TelemetryService::callSession(const QDBusMessage &msg, const Reply &done)
{
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, done] {
        w->deleteLater();
        done(w->reply());
    });
}

QJsonObject TelemetryService::json(const QStringList &wanted)
{
    QJsonObject out;
    for (const QString &kind : wanted) {
        if (kind == u"cpu")
            out[kind] = stale(kind, 900) ? (m_cache[kind] = cpu()) : m_cache.value(kind);
        else if (kind == u"memory")
            out[kind] = stale(kind, 900) ? (m_cache[kind] = memory()) : m_cache.value(kind);
        else if (kind == u"gpu") {
            if (stale(kind, 1900)) {
                m_gpu.sample(); // NVIDIA's numbers arrive with the next call
                m_cache[kind] = QJsonObject{{u"cards"_s, m_gpu.json()}};
            }
            out[kind] = QJsonObject{{u"cards"_s, m_gpu.json()}};
        } else if (kind == u"thermal")
            out[kind] = stale(kind, 1900) ? (m_cache[kind] = sampler::thermal()) : m_cache.value(kind);
        else if (kind == u"net")
            out[kind] = stale(kind, 900) ? (m_cache[kind] = net()) : m_cache.value(kind);
        else if (kind == u"power")
            out[kind] = stale(kind, 4900) ? (m_cache[kind] = power()) : m_cache.value(kind);
        else if (kind == u"media")
            out[kind] = stale(kind, 1900) ? (m_cache[kind] = media()) : m_cache.value(kind);
    }
    return out;
}

QJsonObject TelemetryService::cpu()
{
    const QList<sampler::CpuTimes> now = sampler::readCpuTimes();
    QJsonObject o{{u"cores"_s, jv_cpu_count()}};
    if (!m_cpuPrev.isEmpty() && now.size() == m_cpuPrev.size() && m_cpuClock.elapsed() > 200) {
        const double total = sampler::cpuUsage(m_cpuPrev.first(), now.first());
        o[u"usage"_s] = total;
        push(m_cpuHistory, total);
        QJsonArray per;
        for (int i = 1; i < now.size(); ++i)
            per.append(sampler::cpuUsage(m_cpuPrev.at(i), now.at(i)));
        o[u"perCore"_s] = per;
    } else {
        o[u"usage"_s] = -1;
        o[u"perCore"_s] = QJsonArray();
    }
    m_cpuPrev = now;
    m_cpuClock.start();
    const QList<double> freqs = sampler::cpuFrequencies();
    QJsonArray mhz;
    double sum = 0, top = 0;
    for (double f : freqs) {
        mhz.append(f);
        sum += f;
        top = std::max(top, f);
    }
    o[u"mhz"_s] = mhz;
    o[u"mhzAverage"_s] = freqs.isEmpty() ? -1 : std::round(sum / double(freqs.size()));
    o[u"mhzMax"_s] = freqs.isEmpty() ? -1 : top;
    o[u"history"_s] = m_cpuHistory;
    jv_loadavg load{};
    o[u"load"_s] = jv_loadavg_read(&load) == 0 ? std::round(load.l1 * 100) / 100 : -1.0;
    return o;
}

QJsonObject TelemetryService::memory()
{
    jv_meminfo mem{};
    if (jv_mem_read(&mem) != 0 || mem.total_kb == 0)
        return QJsonObject{{u"percent"_s, -1}};
    const double total = double(mem.total_kb) / 1048576.0, used = double(mem.total_kb - std::min(mem.available_kb, mem.total_kb)) / 1048576.0;
    const double swapTotal = double(mem.swap_total_kb) / 1048576.0, swapUsed = double(mem.swap_total_kb - std::min(mem.swap_free_kb, mem.swap_total_kb)) / 1048576.0;
    return QJsonObject{{u"percent"_s, round1(100.0 * used / total)}, {u"usedGb"_s, round1(used)}, {u"totalGb"_s, round1(total)},
                       {u"swapUsedGb"_s, round1(swapUsed)}, {u"swapTotalGb"_s, round1(swapTotal)}};
}

QJsonObject TelemetryService::net()
{
    const auto now = sampler::readNet();
    const QString iface = sampler::defaultInterface();
    QJsonObject o{{u"interface"_s, iface}};
    double rx = -1, tx = -1;
    if (!m_netPrev.isEmpty() && m_netClock.elapsed() > 200) {
        const double seconds = double(m_netClock.elapsed()) / 1000.0;
        quint64 drx = 0, dtx = 0;
        for (auto it = now.cbegin(); it != now.cend(); ++it) {
            const auto prev = m_netPrev.value(it.key());
            if (it->rx >= prev.rx && it->tx >= prev.tx && m_netPrev.contains(it.key())) {
                drx += it->rx - prev.rx;
                dtx += it->tx - prev.tx;
            }
        }
        rx = std::round(double(drx) / seconds);
        tx = std::round(double(dtx) / seconds);
        push(m_rxHistory, rx);
        push(m_txHistory, tx);
    }
    m_netPrev = now;
    m_netClock.start();
    o[u"rxBytesPerSecond"_s] = rx;
    o[u"txBytesPerSecond"_s] = tx;
    o[u"rxHistory"_s] = m_rxHistory;
    o[u"txHistory"_s] = m_txHistory;
    for (auto it = m_connection.constBegin(); it != m_connection.constEnd(); ++it)
        o[it.key()] = it.value();
    if (stale(u"nm"_s, 9000))
        refreshNetwork();
    return o;
}

void TelemetryService::refreshNetwork()
{
    const QString nm = u"org.freedesktop.NetworkManager"_s;
    callSystem(getProperty(nm, u"/org/freedesktop/NetworkManager"_s, nm, u"ActiveConnections"_s), [this, nm](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            m_connection = QJsonObject{{u"manager"_s, false}};
            return;
        }
        QList<QDBusObjectPath> paths;
        const QVariant v = variantValue(reply);
        if (v.canConvert<QDBusArgument>())
            v.value<QDBusArgument>() >> paths;
        auto collected = std::make_shared<QJsonObject>(QJsonObject{{u"manager"_s, true}, {u"vpn"_s, false}});
        auto pending = std::make_shared<int>(int(paths.size()));
        if (paths.isEmpty()) {
            m_connection = *collected;
            return;
        }
        for (const QDBusObjectPath &p : paths) {
            auto msg = QDBusMessage::createMethodCall(nm, p.path(), u"org.freedesktop.DBus.Properties"_s, u"GetAll"_s);
            msg << u"org.freedesktop.NetworkManager.Connection.Active"_s;
            callSystem(msg, [this, collected, pending](const QDBusMessage &r) {
                const QVariantMap props = propertyMap(r);
                const QString type = props.value(u"Type"_s).toString(), id = props.value(u"Id"_s).toString().left(60);
                if (props.value(u"Vpn"_s).toBool() || type == u"vpn" || type == u"wireguard") {
                    (*collected)[u"vpn"_s] = true;
                    (*collected)[u"vpnName"_s] = id;
                } else if (props.value(u"Default"_s).toBool() || !collected->contains(u"name"_s)) {
                    (*collected)[u"name"_s] = id;
                    (*collected)[u"type"_s] = type.contains(u"wireless"_s) ? u"wifi"_s : type.contains(u"ethernet"_s) ? u"ethernet"_s : type;
                }
                if (--*pending == 0)
                    m_connection = *collected;
            });
        }
    });
}

QJsonObject TelemetryService::power()
{
    QJsonObject o = sampler::battery();
    o[u"profile"_s] = m_powerProfile;
    o[u"powerSaver"_s] = m_powerProfile == u"power-saver";
    refreshPowerProfile();
    return o;
}

void TelemetryService::refreshPowerProfile()
{
    // power-profiles-daemon: the new name first, then the old one (Ubuntu 24.04).
    const auto ask = [this](const QString &service, const QString &path, const QString &iface, const std::function<void()> &otherwise) {
        callSystem(getProperty(service, path, iface, u"ActiveProfile"_s), [this, service, otherwise](const QDBusMessage &reply) {
            if (reply.type() == QDBusMessage::ErrorMessage) {
                if (otherwise)
                    otherwise();
                return;
            }
            m_powerProfile = variantValue(reply).toString().left(20);
            m_powerService = service;
        });
    };
    ask(u"org.freedesktop.UPower.PowerProfiles"_s, u"/org/freedesktop/UPower/PowerProfiles"_s, u"org.freedesktop.UPower.PowerProfiles"_s,
        [ask] { ask(u"net.hadess.PowerProfiles"_s, u"/net/hadess/PowerProfiles"_s, u"net.hadess.PowerProfiles"_s, {}); });
}

QString TelemetryService::setPowerProfile(const QString &profile)
{
    if (!QStringList{u"power-saver"_s, u"balanced"_s, u"performance"_s}.contains(profile))
        return u"unknown profile"_s;
    const bool legacy = m_powerService == u"net.hadess.PowerProfiles";
    const QString service = legacy ? u"net.hadess.PowerProfiles"_s : u"org.freedesktop.UPower.PowerProfiles"_s;
    auto msg = QDBusMessage::createMethodCall(service, legacy ? u"/net/hadess/PowerProfiles"_s : u"/org/freedesktop/UPower/PowerProfiles"_s,
                                              u"org.freedesktop.DBus.Properties"_s, u"Set"_s);
    msg << (legacy ? u"net.hadess.PowerProfiles"_s : u"org.freedesktop.UPower.PowerProfiles"_s) << u"ActiveProfile"_s
        << QVariant::fromValue(QDBusVariant(profile));
    callSystem(msg, [this](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage)
            qWarning().noquote() << "power profile not changed:" << reply.errorMessage();
        m_age.remove(u"power"_s);
        refreshPowerProfile();
    });
    return {};
}

QJsonObject TelemetryService::media()
{
    refreshMedia();
    if (stale(u"audio"_s, 14000))
        refreshAudio();
    QJsonObject o = m_player;
    o[u"audio"_s] = m_audio;
    return o;
}

void TelemetryService::refreshMedia()
{
    auto list = QDBusMessage::createMethodCall(u"org.freedesktop.DBus"_s, u"/org/freedesktop/DBus"_s, u"org.freedesktop.DBus"_s, u"ListNames"_s);
    callSession(list, [this](const QDBusMessage &reply) {
        QStringList players;
        for (const QString &n : reply.arguments().value(0).toStringList())
            if (n.startsWith(u"org.mpris.MediaPlayer2."_s))
                players.append(n);
        if (players.isEmpty()) {
            m_player = QJsonObject{{u"available"_s, false}};
            m_playerName.clear();
            return;
        }
        // The player that plays wins; otherwise the one shown before, otherwise the first.
        auto best = std::make_shared<std::pair<int, QJsonObject>>(-1, QJsonObject());
        auto pending = std::make_shared<int>(int(std::min<qsizetype>(players.size(), 4)));
        for (const QString &name : players.mid(0, 4)) {
            auto msg = QDBusMessage::createMethodCall(name, u"/org/mpris/MediaPlayer2"_s, u"org.freedesktop.DBus.Properties"_s, u"GetAll"_s);
            msg << u"org.mpris.MediaPlayer2.Player"_s;
            callSession(msg, [this, name, best, pending](const QDBusMessage &r) {
                const QVariantMap p = propertyMap(r);
                if (r.type() != QDBusMessage::ErrorMessage) {
                    const QString status = p.value(u"PlaybackStatus"_s).toString();
                    const int rank = status == u"Playing" ? 3 : name == m_playerName ? 2 : status == u"Paused" ? 1 : 0;
                    if (rank > best->first) {
                        QVariantMap meta;
                        const QVariant m = p.value(u"Metadata"_s);
                        if (m.canConvert<QDBusArgument>())
                            meta = qdbus_cast<QVariantMap>(m.value<QDBusArgument>());
                        QString art = metaString(meta, u"mpris:artUrl"_s);
                        // Only local covers: a remote URL would make the widget fetch from the network.
                        if (!art.startsWith(u"file://"_s))
                            art.clear();
                        best->first = rank;
                        best->second = QJsonObject{{u"available"_s, true},
                                                   {u"player"_s, name.mid(23).section(u'.', 0, 0)},
                                                   {u"service"_s, name},
                                                   {u"status"_s, status},
                                                   {u"title"_s, metaString(meta, u"xesam:title"_s).left(200)},
                                                   {u"artist"_s, metaString(meta, u"xesam:artist"_s).left(200)},
                                                   {u"album"_s, metaString(meta, u"xesam:album"_s).left(200)},
                                                   {u"artUrl"_s, art.left(1000)},
                                                   {u"volume"_s, p.contains(u"Volume"_s) ? p.value(u"Volume"_s).toDouble() : -1},
                                                   {u"canNext"_s, p.value(u"CanGoNext"_s).toBool()},
                                                   {u"canPrevious"_s, p.value(u"CanGoPrevious"_s).toBool()},
                                                   {u"canPlay"_s, p.value(u"CanPlay"_s).toBool() || p.value(u"CanPause"_s).toBool()}};
                    }
                }
                if (--*pending == 0) {
                    m_player = best->first >= 0 ? best->second : QJsonObject{{u"available"_s, false}};
                    m_playerName = m_player.value(u"service"_s).toString();
                }
            });
        }
    });
}

void TelemetryService::refreshAudio()
{
    // PipeWire/PulseAudio through pactl: the default output's name and volume.
    if (QStandardPaths::findExecutable(u"pactl"_s).isEmpty() || m_pactl.state() != QProcess::NotRunning)
        return;
    m_pactl.disconnect(this);
    connect(&m_pactl, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QByteArray out = m_pactl.readAllStandardOutput();
        if (code != 0)
            return;
        // `pactl info`: "Default Sink: alsa_output…"; then the sinks as JSON.
        if (m_pactl.arguments().value(0) == u"info") {
            for (const QByteArray &line : out.split('\n'))
                if (line.startsWith("Default Sink:"))
                    m_audio[u"sink"_s] = QString::fromUtf8(line.mid(13).trimmed());
            m_pactl.start(u"pactl"_s, {u"-f"_s, u"json"_s, u"list"_s, u"sinks"_s});
            return;
        }
        for (const QJsonValue &v : QJsonDocument::fromJson(out).array()) {
            const QJsonObject sink = v.toObject();
            if (sink.value(u"name"_s).toString() != m_audio.value(u"sink"_s).toString())
                continue;
            m_audio[u"output"_s] = sink.value(u"description"_s).toString().left(80);
            m_audio[u"muted"_s] = sink.value(u"mute"_s).toBool();
            const QJsonObject channels = sink.value(u"volume"_s).toObject();
            if (!channels.isEmpty()) {
                const QString percent = channels.begin()->toObject().value(u"value_percent"_s).toString();
                m_audio[u"volume"_s] = percent.left(percent.size() - 1).trimmed().toDouble();
            }
        }
    });
    m_pactl.start(u"pactl"_s, {u"info"_s});
}

QString TelemetryService::mediaControl(const QString &action)
{
    if (action == u"volume-up" || action == u"volume-down" || action == u"mute") {
        if (QStandardPaths::findExecutable(u"pactl"_s).isEmpty())
            return u"pactl is not installed"_s;
        const QStringList args = action == u"mute" ? QStringList{u"set-sink-mute"_s, u"@DEFAULT_SINK@"_s, u"toggle"_s}
                                                   : QStringList{u"set-sink-volume"_s, u"@DEFAULT_SINK@"_s, action == u"volume-up" ? u"+5%"_s : u"-5%"_s};
        auto *p = new QProcess(this);
        connect(p, &QProcess::finished, p, &QObject::deleteLater);
        connect(p, &QProcess::finished, this, [this] {
            m_age.remove(u"audio"_s);
            m_age.remove(u"media"_s);
        });
        p->start(u"pactl"_s, args);
        return {};
    }
    static const QHash<QString, QString> methods{{u"play-pause"_s, u"PlayPause"_s}, {u"next"_s, u"Next"_s}, {u"previous"_s, u"Previous"_s}};
    if (!methods.contains(action))
        return u"unknown action"_s;
    if (m_playerName.isEmpty())
        return u"no media player"_s;
    callSession(QDBusMessage::createMethodCall(m_playerName, u"/org/mpris/MediaPlayer2"_s, u"org.mpris.MediaPlayer2.Player"_s, methods.value(action)),
                [this](const QDBusMessage &) { m_age.remove(u"media"_s); });
    return {};
}

QString TelemetryService::describe(const QString &kind)
{
    const QJsonObject all = json({kind});
    const QJsonObject o = all.value(kind).toObject();
    const auto pct = [](double v) { return v < 0 ? u"—"_s : u"%1 %"_s.arg(qRound(v)); };
    if (kind == u"gpu") {
        QStringList parts;
        for (const QJsonValue &v : o.value(u"cards"_s).toArray()) {
            const QJsonObject c = v.toObject();
            QString line = c.value(u"name"_s).toString() + u": "_s
                           + (c.value(u"asleep"_s).toBool() ? u"asleep"_s : pct(c.value(u"load"_s).toDouble()));
            if (c.value(u"vramTotalMb"_s).toDouble() > 0)
                line += u" · VRAM %1/%2 GB"_s.arg(c.value(u"vramUsedMb"_s).toDouble() / 1024, 0, 'f', 1).arg(c.value(u"vramTotalMb"_s).toDouble() / 1024, 0, 'f', 1);
            if (c.value(u"temperature"_s).toDouble() > 0)
                line += u" · %1 °C"_s.arg(qRound(c.value(u"temperature"_s).toDouble()));
            parts.append(line);
        }
        return parts.isEmpty() ? u"GPU: —"_s : parts.join(u"; "_s);
    }
    if (kind == u"memory")
        return u"RAM: %1 · %2/%3 GB"_s.arg(pct(o.value(u"percent"_s).toDouble())).arg(o.value(u"usedGb"_s).toDouble()).arg(o.value(u"totalGb"_s).toDouble());
    if (kind == u"net")
        return u"%1 · ↓ %2 KB/s · ↑ %3 KB/s"_s.arg(o.value(u"name"_s).toString(o.value(u"interface"_s).toString()))
            .arg(qRound(std::max(0.0, o.value(u"rxBytesPerSecond"_s).toDouble()) / 1024))
            .arg(qRound(std::max(0.0, o.value(u"txBytesPerSecond"_s).toDouble()) / 1024));
    if (kind == u"power")
        return o.value(u"present"_s).toBool() ? u"Battery: %1 % · %2"_s.arg(o.value(u"percent"_s).toDouble()).arg(o.value(u"status"_s).toString())
                                              : u"No battery"_s;
    if (kind == u"thermal") {
        double top = -1;
        for (const QJsonValue &v : o.value(u"sensors"_s).toArray())
            top = std::max(top, v.toObject().value(u"celsius"_s).toDouble());
        return top < 0 ? u"Temperature: —"_s : u"Hottest sensor: %1 °C"_s.arg(qRound(top));
    }
    return u"CPU: %1 · %2 cores · %3 MHz"_s.arg(pct(o.value(u"usage"_s).toDouble())).arg(o.value(u"cores"_s).toInt()).arg(o.value(u"mhzAverage"_s).toInt());
}

} // namespace jarvis
