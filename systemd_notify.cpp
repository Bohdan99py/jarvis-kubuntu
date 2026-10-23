#include "systemd_notify.h"

#include <QDebug>
#include <QtGlobal>

#include <cstddef>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace jarvis::systemd {

bool notify(const QByteArray &state)
{
    const QByteArray path = qgetenv("NOTIFY_SOCKET");
    sockaddr_un addr{};
    if (path.isEmpty() || (path[0] != '/' && path[0] != '@') || size_t(path.size()) >= sizeof(addr.sun_path))
        return false;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.constData(), size_t(path.size()));
    if (path[0] == '@')
        addr.sun_path[0] = '\0'; // abstract namespace
    const socklen_t len = socklen_t(offsetof(sockaddr_un, sun_path) + size_t(path.size()));

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    const ssize_t sent = ::sendto(fd, state.constData(), size_t(state.size()), MSG_NOSIGNAL,
                                  reinterpret_cast<const sockaddr *>(&addr), len);
    ::close(fd);
    return sent == state.size();
}

qint64 watchdogIntervalMs()
{
    bool ok = false;
    const qint64 usec = qEnvironmentVariable("WATCHDOG_USEC").toLongLong(&ok);
    if (!ok || usec <= 0)
        return 0;
    // The watchdog is meant for this very process, not for one started from it.
    const QByteArray pid = qgetenv("WATCHDOG_PID");
    if (!pid.isEmpty() && pid.toLongLong() != qint64(::getpid()))
        return 0;
    return usec / 1000;
}

Watchdog::Watchdog(QObject *parent)
    : QObject(parent)
{
    const qint64 interval = watchdogIntervalMs();
    if (interval <= 0)
        return;
    // Half the interval, as sd_watchdog_enabled(3) recommends; never busier than every 5 s.
    m_ping.setInterval(int(qMax<qint64>(5000, interval / 2)));
    m_ping.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_ping, &QTimer::timeout, this, [] { notify("WATCHDOG=1"); });
    m_ping.start();
    notify("WATCHDOG=1");
}

void Watchdog::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    notify("STATUS=" + status.left(200).toUtf8());
}

void Watchdog::stopping()
{
    m_ping.stop();
    notify("STOPPING=1");
}

} // namespace jarvis::systemd
