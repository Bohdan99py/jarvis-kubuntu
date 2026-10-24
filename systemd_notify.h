#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace jarvis::systemd {

// sd_notify(3) without libsystemd: one datagram to $NOTIFY_SOCKET. Does
// nothing (and returns false) when the process is not run by systemd.
bool notify(const QByteArray &state);
// The watchdog interval systemd expects for this process (WatchdogSec=), in
// milliseconds; 0 when there is no watchdog.
qint64 watchdogIntervalMs();

// Tells systemd the daemon is alive (WATCHDOG=1) at half the watchdog
// interval and keeps STATUS= (shown by `systemctl --user status jarvis`) up to
// date. A hung event loop stops the pings and systemd restarts the service.
class Watchdog : public QObject
{
    Q_OBJECT

public:
    explicit Watchdog(QObject *parent = nullptr);
    void setStatus(const QString &status);
    void stopping();

private:
    QTimer m_ping;
    QString m_status;
};

} // namespace jarvis::systemd
