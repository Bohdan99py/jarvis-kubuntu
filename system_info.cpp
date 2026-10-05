#include "system_info.h"

#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <cstdint>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr int kSampleIntervalMs = 2000;
constexpr int kTopProcesses = 5;

inline QLocale localeFor(bool ru)
{
    return QLocale(ru ? QLocale::Russian : QLocale::English);
}

inline QString pick(bool ru, const QString &r, const QString &e)
{
    return ru ? r : e;
}

QString gib(quint64 kb, const QLocale &loc)
{
    return loc.toString(double(kb) / 1048576.0, 'f', 1);
}

QString gibBytes(quint64 bytes, const QLocale &loc)
{
    return loc.toString(double(bytes) / 1073741824.0, 'f', 1);
}

QString formatKb(quint64 kb, bool ru, const QLocale &loc)
{
    if (kb >= 1048576)
        return gib(kb, loc) + u" "_s + pick(ru, u"ГиБ"_s, u"GiB"_s);
    return QString::number(kb / 1024) + u" "_s + pick(ru, u"МиБ"_s, u"MiB"_s);
}

QString translateStatus(const QString &status, bool ru)
{
    if (!ru)
        return status.toLower();
    if (status == u"Charging")
        return u"заряжается"_s;
    if (status == u"Discharging")
        return u"разряжается"_s;
    if (status == u"Full")
        return u"полностью заряжена"_s;
    if (status == u"Not charging")
        return u"не заряжается"_s;
    return status.toLower();
}

} // namespace

SystemInfo::SystemInfo(QObject *parent)
    : QObject(parent)
{
    if (jv_cpu_times_read(&m_last) == 0)
        m_samples = 1;

    m_timer.setInterval(kSampleIntervalMs);
    m_timer.setTimerType(Qt::VeryCoarseTimer); // no need for precision, saves wakeups
    connect(&m_timer, &QTimer::timeout, this, &SystemInfo::sample);
    m_timer.start();
}

void SystemInfo::sample()
{
    jv_cpu_times now{};
    if (jv_cpu_times_read(&now) != 0)
        return;
    m_prev = m_last;
    m_last = now;
    if (m_samples < 2)
        ++m_samples;
}

QString SystemInfo::cpuReport(bool ru) const
{
    const QLocale loc = localeFor(ru);

    // Before the second sample exists, fall back to the average since boot.
    const jv_cpu_times zero{};
    const double usage = jv_cpu_usage_between(m_samples >= 2 ? &m_prev : &zero, &m_last);
    const int cores = jv_cpu_count();

    QString text = pick(ru,
                        u"Процессор: загрузка %1%, ядер: %2."_s,
                        u"CPU: %1% load, %2 cores."_s)
                       .arg(loc.toString(usage, 'f', 1))
                       .arg(cores);

    jv_loadavg la;
    if (jv_loadavg_read(&la) == 0) {
        text += u" "_s + pick(ru, u"Средняя нагрузка: %1 / %2 / %3."_s,
                            u"Load average: %1 / %2 / %3."_s)
                           .arg(loc.toString(la.l1, 'f', 2),
                                loc.toString(la.l5, 'f', 2),
                                loc.toString(la.l15, 'f', 2));
    }
    return text;
}

QString SystemInfo::memoryReport(bool ru) const
{
    jv_meminfo m;
    if (jv_mem_read(&m) != 0)
        return pick(ru, u"Не удалось прочитать информацию о памяти."_s,
                    u"Couldn't read memory info."_s);

    const QLocale loc = localeFor(ru);
    const quint64 used = m.total_kb > m.available_kb ? m.total_kb - m.available_kb : 0;
    const double pct = m.total_kb ? 100.0 * double(used) / double(m.total_kb) : 0.0;
    const QString unit = pick(ru, u"ГиБ"_s, u"GiB"_s);

    QString text = pick(ru,
                        u"Память: занято %1 из %2 %3 (%4%), доступно %5 %3."_s,
                        u"Memory: %1 of %2 %3 in use (%4%), %5 %3 available."_s)
                       .arg(gib(used, loc), gib(m.total_kb, loc), unit,
                            loc.toString(pct, 'f', 0), gib(m.available_kb, loc));

    if (m.swap_total_kb > 0) {
        const quint64 swapUsed = m.swap_total_kb - std::min(m.swap_free_kb, m.swap_total_kb);
        text += u" "_s + pick(ru, u"Swap: %1 из %2 %3."_s, u"Swap: %1 of %2 %3."_s)
                           .arg(gib(swapUsed, loc), gib(m.swap_total_kb, loc), unit);
    }
    return text;
}

QString SystemInfo::batteryReport(bool ru) const
{
    jv_battery b;
    if (jv_battery_read(&b) != 0 || !b.present)
        return pick(ru, u"Батареи нет — похоже, это стационарный компьютер."_s,
                    u"No battery found — looks like a desktop machine."_s);

    const QString status = translateStatus(QString::fromUtf8(b.status), ru);
    if (b.percent < 0)
        return pick(ru, u"Батарея: %1."_s, u"Battery: %1."_s).arg(status);
    return pick(ru, u"Батарея: %1%, %2."_s, u"Battery: %1%, %2."_s).arg(b.percent).arg(status);
}

QString SystemInfo::temperatureReport(bool ru) const
{
    double c = 0.0;
    if (jv_thermal_max_c(&c) != 0)
        return pick(ru, u"Датчики температуры недоступны."_s,
                    u"Temperature sensors are not available."_s);

    return pick(ru, u"Температура: самый горячий датчик показывает %1 °C."_s,
                u"Temperature: the hottest sensor reads %1 °C."_s)
        .arg(localeFor(ru).toString(c, 'f', 0));
}

QString SystemInfo::uptimeReport(bool ru) const
{
    double secs = 0.0;
    if (jv_uptime_read(&secs) != 0)
        return pick(ru, u"Не удалось узнать аптайм."_s, u"Couldn't read uptime."_s);

    const qint64 total = qint64(secs);
    const qint64 days = total / 86400;
    const qint64 hours = (total % 86400) / 3600;
    const qint64 mins = (total % 3600) / 60;

    QStringList parts;
    if (days > 0)
        parts << pick(ru, u"%1 д"_s, u"%1 d"_s).arg(days);
    if (days > 0 || hours > 0)
        parts << pick(ru, u"%1 ч"_s, u"%1 h"_s).arg(hours);
    parts << pick(ru, u"%1 мин"_s, u"%1 min"_s).arg(mins);

    return pick(ru, u"Система работает уже %1."_s, u"The system has been up for %1."_s)
        .arg(parts.join(QChar(u' ')));
}

QString SystemInfo::diskReport(bool ru) const
{
    jv_disk d;
    if (jv_disk_read("/", &d) != 0)
        return pick(ru, u"Не удалось прочитать информацию о диске."_s,
                    u"Couldn't read disk info."_s);

    const QLocale loc = localeFor(ru);
    const quint64 used = d.total_bytes > d.avail_bytes ? d.total_bytes - d.avail_bytes : 0;
    const QString unit = pick(ru, u"ГиБ"_s, u"GiB"_s);

    return pick(ru, u"Диск /: занято %1 из %2 %3, свободно %4 %3."_s,
                u"Disk /: %1 of %2 %3 used, %4 %3 free."_s)
        .arg(gibBytes(used, loc), gibBytes(d.total_bytes, loc), unit,
             gibBytes(d.avail_bytes, loc));
}

QString SystemInfo::systemReport(bool ru) const
{
    jv_osinfo os;
    if (jv_osinfo_read(&os) != 0)
        return pick(ru, u"Не удалось прочитать информацию о системе."_s,
                    u"Couldn't read system info."_s);

    return pick(ru, u"Система: %1, ядро %2, %3, хост %4."_s,
                u"System: %1, kernel %2, %3, host %4."_s)
        .arg(QString::fromUtf8(os.pretty), QString::fromUtf8(os.kernel),
             QString::fromUtf8(os.machine), QString::fromUtf8(os.hostname));
}

QString SystemInfo::processesReport(bool ru) const
{
    jv_proc procs[kTopProcesses];
    const int n = jv_proc_top_mem(procs, kTopProcesses);
    if (n <= 0)
        return pick(ru, u"Не удалось получить список процессов."_s,
                    u"Couldn't read the process list."_s);

    const QLocale loc = localeFor(ru);
    QStringList lines;
    lines.reserve(n + 1);
    lines << pick(ru, u"Топ-%1 процессов по памяти:"_s, u"Top %1 processes by memory:"_s).arg(n);
    for (int i = 0; i < n; ++i) {
        lines << u"%1. %2 — %3"_s.arg(i + 1)
                     .arg(QString::fromUtf8(procs[i].comm), formatKb(procs[i].rss_kb, ru, loc));
    }
    return lines.join(QChar(u'\n'));
}

} // namespace jarvis
