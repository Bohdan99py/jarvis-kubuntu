#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// What the desktop widgets show about the machine, read from /proc and /sys
// (0.19.2). Pure functions over a root directory, so tests can give them a fake
// /proc and /sys; jarvisd calls them only when a widget asks, and caches.
// Nothing here guesses: a value that cannot be read is -1 or absent.
namespace jarvis::sampler {

struct CpuTimes
{
    quint64 busy = 0;
    quint64 total = 0;
};

// "cpu" (all) first, then cpu0, cpu1… from <root>/proc/stat.
QList<CpuTimes> readCpuTimes(const QString &root = QStringLiteral("/"));
// Percent busy between two readings of the same CPU; -1 if nothing elapsed.
double cpuUsage(const CpuTimes &before, const CpuTimes &after);
// Current frequency of each CPU in MHz (cpufreq), in CPU order; empty without cpufreq.
QList<double> cpuFrequencies(const QString &root = QStringLiteral("/"));

// hwmon: {sensors: [{chip, label, celsius}], fans: [{chip, label, rpm}]}.
QJsonObject thermal(const QString &root = QStringLiteral("/"));

struct NetCounters
{
    quint64 rx = 0;
    quint64 tx = 0;
};
// Bytes received and sent per interface (<root>/proc/net/dev), loopback excluded.
QHash<QString, NetCounters> readNet(const QString &root = QStringLiteral("/"));
// The interface carrying the default route (<root>/proc/net/route), "" if none.
QString defaultInterface(const QString &root = QStringLiteral("/"));

// <root>/sys/class/power_supply: {present, percent, status, charging, onAc,
// minutesLeft (to empty or to full; -1 without a sound estimate), watts}.
QJsonObject battery(const QString &root = QStringLiteral("/"));

} // namespace jarvis::sampler
