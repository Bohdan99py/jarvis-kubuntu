#include "system_sampler.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace jarvis::sampler {
namespace {

QString path(const QString &root, const QString &relative)
{
    return QDir::cleanPath(root + u'/' + relative);
}

QByteArray readAll(const QString &file, qint64 max = 65536)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.read(max) : QByteArray();
}

QString readText(const QString &file)
{
    return QString::fromUtf8(readAll(file, 4096)).trimmed();
}

double readNumber(const QString &file, double scale = 1)
{
    bool ok = false;
    const double v = readText(file).toDouble(&ok);
    return ok ? v / scale : -1;
}

double round1(double v)
{
    return std::round(v * 10) / 10;
}

} // namespace

QList<CpuTimes> readCpuTimes(const QString &root)
{
    QList<CpuTimes> out;
    const QList<QByteArray> lines = readAll(path(root, u"proc/stat"_s), 1 << 20).split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("cpu"))
            break; // the cpu lines come first
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 5)
            continue;
        CpuTimes t;
        // user nice system idle iowait irq softirq steal
        for (int i = 1; i < f.size() && i <= 8; ++i) {
            const quint64 v = f.at(i).toULongLong();
            t.total += v;
            if (i != 4 && i != 5) // idle and iowait are not work
                t.busy += v;
        }
        out.append(t);
    }
    return out;
}

double cpuUsage(const CpuTimes &before, const CpuTimes &after)
{
    if (after.total <= before.total || after.busy < before.busy)
        return -1;
    return round1(100.0 * double(after.busy - before.busy) / double(after.total - before.total));
}

QList<double> cpuFrequencies(const QString &root)
{
    QList<double> out;
    const QDir dir(path(root, u"sys/devices/system/cpu"_s));
    static const QRegularExpression cpuName(u"^cpu(\\d+)$"_s);
    QList<std::pair<int, double>> found;
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto m = cpuName.match(entry);
        if (!m.hasMatch())
            continue;
        const double khz = readNumber(dir.filePath(entry) + u"/cpufreq/scaling_cur_freq"_s);
        if (khz > 0)
            found.append({m.captured(1).toInt(), std::round(khz / 1000.0)});
    }
    std::sort(found.begin(), found.end());
    for (const auto &[index, mhz] : found)
        out.append(mhz);
    return out;
}

QJsonObject thermal(const QString &root)
{
    QJsonArray sensors, fans;
    const QDir dir(path(root, u"sys/class/hwmon"_s));
    static const QRegularExpression input(u"^(temp|fan)(\\d+)_input$"_s);
    for (const QString &hw : dir.entryList({u"hwmon*"_s}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        const QDir chip(dir.filePath(hw));
        const QString chipName = readText(chip.filePath(u"name"_s));
        for (const QString &file : chip.entryList(QDir::Files | QDir::System, QDir::Name)) {
            const auto m = input.match(file);
            if (!m.hasMatch())
                continue;
            const QString kind = m.captured(1), n = m.captured(2);
            QString label = readText(chip.filePath(kind + n + u"_label"_s));
            if (label.isEmpty())
                label = kind == u"temp" ? u"temp%1"_s.arg(n) : u"fan%1"_s.arg(n);
            if (kind == u"temp") {
                const double c = readNumber(chip.filePath(file), 1000.0);
                if (c > -40 && c < 150) // unplugged sensors report nonsense
                    sensors.append(QJsonObject{{u"chip"_s, chipName}, {u"label"_s, label}, {u"celsius"_s, round1(c)}});
            } else {
                const double rpm = readNumber(chip.filePath(file));
                if (rpm >= 0)
                    fans.append(QJsonObject{{u"chip"_s, chipName}, {u"label"_s, label}, {u"rpm"_s, rpm}});
            }
        }
    }
    return QJsonObject{{u"sensors"_s, sensors}, {u"fans"_s, fans}};
}

QHash<QString, NetCounters> readNet(const QString &root)
{
    QHash<QString, NetCounters> out;
    const QList<QByteArray> lines = readAll(path(root, u"proc/net/dev"_s)).split('\n');
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString name = QString::fromLatin1(line.left(colon).trimmed());
        if (name == u"lo")
            continue;
        const QList<QByteArray> f = line.mid(colon + 1).simplified().split(' ');
        if (f.size() < 9)
            continue;
        out.insert(name, NetCounters{f.at(0).toULongLong(), f.at(8).toULongLong()});
    }
    return out;
}

QString defaultInterface(const QString &root)
{
    // Iface Destination Gateway Flags … : the default route has destination 00000000.
    const QList<QByteArray> lines = readAll(path(root, u"proc/net/route"_s)).split('\n');
    for (const QByteArray &line : lines.mid(1)) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() > 2 && f.at(1) == "00000000")
            return QString::fromLatin1(f.at(0));
    }
    return {};
}

QJsonObject battery(const QString &root)
{
    QJsonObject out{{u"present"_s, false}, {u"percent"_s, -1}, {u"charging"_s, false}, {u"onAc"_s, false}, {u"minutesLeft"_s, -1}, {u"watts"_s, -1}};
    const QDir dir(path(root, u"sys/class/power_supply"_s));
    bool ac = false;
    for (const QString &supply : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        const QString base = dir.filePath(supply) + u'/';
        const QString type = readText(base + u"type"_s);
        if (type == u"Mains") {
            ac = ac || readText(base + u"online"_s) == u"1";
            continue;
        }
        if (type != u"Battery" || out.value(u"present"_s).toBool() || readText(base + u"scope"_s) == u"Device")
            continue; // the first system battery; mice and headsets have scope Device
        out[u"present"_s] = readText(base + u"present"_s) != u"0";
        const QString status = readText(base + u"status"_s);
        out[u"status"_s] = status;
        out[u"charging"_s] = status == u"Charging";
        out[u"percent"_s] = readNumber(base + u"capacity"_s);
        // Energy (µWh, µW) or charge (µAh, µA): either gives a time estimate.
        double now = readNumber(base + u"energy_now"_s), full = readNumber(base + u"energy_full"_s), rate = readNumber(base + u"power_now"_s);
        if (now < 0) {
            now = readNumber(base + u"charge_now"_s);
            full = readNumber(base + u"charge_full"_s);
            rate = readNumber(base + u"current_now"_s);
        } else if (rate > 0) {
            out[u"watts"_s] = round1(rate / 1e6);
        }
        // Below a trickle the estimate is meaningless (a full battery, a calibration): none.
        if (rate > 1e5 && now >= 0) {
            if (status == u"Discharging")
                out[u"minutesLeft"_s] = std::round(60.0 * now / rate);
            else if (status == u"Charging" && full > now)
                out[u"minutesLeft"_s] = std::round(60.0 * (full - now) / rate);
        }
    }
    out[u"onAc"_s] = ac;
    return out;
}

} // namespace jarvis::sampler
