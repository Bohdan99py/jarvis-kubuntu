#pragma once

#include <QDBusMessage>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

#include <functional>

#include "gpu_devices.h"
#include "system_sampler.h"

namespace jarvis {

// The one source of live numbers for every desktop widget (0.19.2). Nothing
// runs in the background: a kind is measured when a widget asks for it and
// is then cached for its minimum interval, so ten widgets cost what one does.
// Reads of /proc and /sys are quick and done in place; the D-Bus questions
// (NetworkManager, power profiles, media players) and the processes
// (nvidia-smi, pactl) are asynchronous — their answer is in the next reply.
//
// Kinds: cpu, memory, gpu, thermal, net, power, media.
class TelemetryService : public QObject
{
    Q_OBJECT

public:
    explicit TelemetryService(QObject *parent = nullptr);

    static QStringList kinds();
    QJsonObject json(const QStringList &kinds);
    // play-pause, next, previous, volume-up, volume-down, mute. "" or why not.
    QString mediaControl(const QString &action);
    // power-saver, balanced, performance (power-profiles-daemon). "" or why not.
    QString setPowerProfile(const QString &profile);
    // One line for KRunner and the chat: "GPU AMD Radeon 680M: 12 % · VRAM 0.4/2 GB · 45 °C".
    QString describe(const QString &kind);

private:
    using Reply = std::function<void(const QDBusMessage &)>;
    void callSystem(const QDBusMessage &msg, const Reply &done);
    void callSession(const QDBusMessage &msg, const Reply &done);
    bool stale(const QString &kind, qint64 minMs);
    QJsonObject cpu();
    QJsonObject memory();
    QJsonObject net();
    QJsonObject power();
    QJsonObject media();
    void refreshNetwork();
    void refreshPowerProfile();
    void refreshMedia();
    void refreshAudio();

    GpuMonitor m_gpu;
    QHash<QString, QElapsedTimer> m_age;
    QHash<QString, QJsonObject> m_cache;

    QList<sampler::CpuTimes> m_cpuPrev;
    QElapsedTimer m_cpuClock;
    QJsonArray m_cpuHistory;

    QHash<QString, sampler::NetCounters> m_netPrev;
    QElapsedTimer m_netClock;
    QJsonArray m_rxHistory, m_txHistory;
    QJsonObject m_connection; // {name, type, vpn, vpnName}

    QString m_powerProfile;
    QString m_powerService;   // which power-profiles API answered

    QJsonObject m_player{{QStringLiteral("available"), false}}; // the media player shown (none until MPRIS answers)
    QString m_playerName;     // org.mpris.MediaPlayer2.…
    QString m_pinnedPlayer;   // the one the user switched to (0.19.3): shown first while it exists
    QStringList m_players;    // every MPRIS player on the bus (up to four)
    QJsonObject m_audio;      // {output, volume, muted}
    QProcess m_pactl;
};

} // namespace jarvis
