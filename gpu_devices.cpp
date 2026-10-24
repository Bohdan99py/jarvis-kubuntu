#include "gpu_devices.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.read(256)).trimmed() : QString();
}

double readNumber(const QString &path, double scale = 1)
{
    bool ok = false;
    const double v = readText(path).toDouble(&ok);
    return ok ? v / scale : -1;
}

// Device names from the PCI ID database (hwdata), when it is installed.
QHash<QString, QString> pciNames(const QSet<QString> &wanted)
{
    QHash<QString, QString> names;
    for (const char *path : {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"}) {
        QFile f(QString::fromLatin1(path));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QString vendor;
        while (!f.atEnd() && names.size() < wanted.size()) {
            const QByteArray line = f.readLine();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            if (line.at(0) != '\t') { // "10de  NVIDIA Corporation"
                vendor = QString::fromLatin1(line.left(4)).toLower();
                continue;
            }
            if (line.size() < 8 || line.at(1) == '\t') // subsystems
                continue;
            const QString key = vendor + u':' + QString::fromLatin1(line.mid(1, 4)).toLower(); // "\t25a2  GA107M [GeForce RTX 3050 Mobile]"
            if (wanted.contains(key))
                names.insert(key, QString::fromUtf8(line.mid(7)).trimmed());
        }
        break;
    }
    return names;
}

// The first hwmon directory of a device (amdgpu, i915 and nouveau have one).
QString hwmonOf(const QString &devicePath)
{
    const QDir dir(devicePath + u"/hwmon"_s);
    const QStringList entries = dir.entryList({u"hwmon*"_s}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    return entries.isEmpty() ? QString() : dir.filePath(entries.first());
}

} // namespace

QString gpuVendorName(const QString &id)
{
    if (id == u"10de")
        return u"NVIDIA"_s;
    if (id == u"1002")
        return u"AMD"_s;
    if (id == u"8086")
        return u"Intel"_s;
    if (id == u"1af4")
        return u"Virtio"_s;
    if (id == u"15ad")
        return u"VMware"_s;
    if (id == u"1234")
        return u"QEMU"_s;
    return id;
}

QList<GpuDevice> detectGpus()
{
    QList<GpuDevice> list;
    QSet<QString> wanted;
    static const QRegularExpression cardName(u"^card\\d+$"_s);
    const QDir drm(qEnvironmentVariable("JARVIS_SYSFS_ROOT", u"/sys"_s) + u"/class/drm"_s);
    for (const QString &card : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (!cardName.match(card).hasMatch())
            continue;
        GpuDevice g;
        g.card = card;
        g.devicePath = drm.filePath(card) + u"/device"_s;
        g.vendorId = readText(g.devicePath + u"/vendor"_s).remove(u"0x"_s).toLower();
        g.deviceId = readText(g.devicePath + u"/device"_s).remove(u"0x"_s).toLower();
        if (g.vendorId.isEmpty())
            continue;
        g.slot = QFileInfo(QFileInfo(g.devicePath).canonicalFilePath()).fileName();
        g.driver = QFileInfo(QFileInfo(g.devicePath + u"/driver"_s).canonicalFilePath()).fileName();
        g.boot = readText(g.devicePath + u"/boot_vga"_s) == u"1";
        if (std::any_of(list.cbegin(), list.cend(), [&](const GpuDevice &o) { return o.slot == g.slot; }))
            continue;
        wanted.insert(g.vendorId + u':' + g.deviceId);
        list.append(g);
    }
    const QHash<QString, QString> names = pciNames(wanted);
    for (GpuDevice &g : list) {
        QString n = names.value(g.vendorId + u':' + g.deviceId);
        // "GA107M [GeForce RTX 3050 Mobile]" → "GeForce RTX 3050 Mobile"
        const qsizetype open = n.indexOf(u'['), close = n.lastIndexOf(u']');
        if (open >= 0 && close > open)
            n = n.mid(open + 1, close - open - 1);
        g.name = gpuVendorName(g.vendorId) + u' ' + (n.isEmpty() ? u"GPU %1"_s.arg(g.deviceId) : n);
    }
    return list;
}

GpuMonitor::GpuMonitor(QObject *parent)
    : QObject(parent)
{
    for (const GpuDevice &g : detectGpus()) {
        Card card;
        card.device = g;
        card.name = g.name;
        m_cards.append(card);
    }
    m_noNvidia = QStandardPaths::findExecutable(u"nvidia-smi"_s).isEmpty();
    connect(&m_nvidia, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (code != 0) {
            m_noNvidia = true;
            return;
        }
        // "00000000:01:00.0, NVIDIA GeForce RTX 3050 Laptop GPU, 12, 1043, 6144, 48, 1350" per card
        for (const QString &line : QString::fromUtf8(m_nvidia.readAllStandardOutput()).split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList f = line.split(u',');
            if (f.size() < 5)
                continue;
            for (Card &card : m_cards) {
                if (card.device.driver != u"nvidia" || !f.at(0).trimmed().toLower().endsWith(card.device.slot.mid(5).toLower()))
                    continue;
                card.name = f.at(1).trimmed().left(60);
                card.load = f.at(2).trimmed().toDouble();
                card.vramUsedMb = f.at(3).trimmed().toDouble();
                card.vramTotalMb = f.at(4).trimmed().toDouble();
                card.memory = card.vramTotalMb > 0 ? 100.0 * card.vramUsedMb / card.vramTotalMb : -1;
                bool ok = false;
                card.temperature = f.value(5).trimmed().toDouble(&ok);
                if (!ok)
                    card.temperature = -1;
                card.clockMhz = f.value(6).trimmed().toDouble(&ok);
                if (!ok)
                    card.clockMhz = -1;
            }
        }
        emit updated();
    });
}

GpuMonitor::~GpuMonitor()
{
    if (m_nvidia.state() != QProcess::NotRunning) {
        m_nvidia.kill();
        m_nvidia.waitForFinished(500);
    }
}

void GpuMonitor::sample()
{
    bool askNvidia = false;
    for (Card &card : m_cards) {
        const QString dev = card.device.devicePath;
        if (card.device.driver == u"nvidia") {
            // A runtime-suspended card is idle; asking nvidia-smi would wake it up.
            card.asleep = readText(dev + u"/power/runtime_status"_s) == u"suspended";
            if (card.asleep) {
                card.load = 0;
                card.memory = card.vramUsedMb = card.temperature = card.clockMhz = -1;
            } else {
                askNvidia = true;
            }
            continue;
        }
        // AMD and Intel report through sysfs.
        card.load = readNumber(dev + u"/gpu_busy_percent"_s);
        const double used = readNumber(dev + u"/mem_info_vram_used"_s, 1024.0 * 1024.0);
        const double total = readNumber(dev + u"/mem_info_vram_total"_s, 1024.0 * 1024.0);
        card.vramUsedMb = used;
        card.vramTotalMb = total;
        card.memory = used >= 0 && total > 0 ? 100.0 * used / total : -1;
        const QString hwmon = hwmonOf(dev);
        card.temperature = hwmon.isEmpty() ? -1 : readNumber(hwmon + u"/temp1_input"_s, 1000.0);
        card.clockMhz = hwmon.isEmpty() ? -1 : readNumber(hwmon + u"/freq1_input"_s, 1e6);
    }
    if (!askNvidia || m_noNvidia || m_nvidia.state() != QProcess::NotRunning) {
        emit updated();
        return;
    }
    m_nvidia.start(u"nvidia-smi"_s, {u"--query-gpu=pci.bus_id,name,utilization.gpu,memory.used,memory.total,temperature.gpu,clocks.gr"_s,
                                     u"--format=csv,noheader,nounits"_s});
}

QJsonArray GpuMonitor::json() const
{
    QJsonArray out;
    for (const Card &c : m_cards)
        out.append(QJsonObject{{u"name"_s, c.name},
                               {u"driver"_s, c.device.driver},
                               {u"slot"_s, c.device.slot},
                               {u"boot"_s, c.device.boot},
                               {u"load"_s, c.load},
                               {u"memory"_s, c.memory},
                               {u"vramUsedMb"_s, c.vramUsedMb},
                               {u"vramTotalMb"_s, c.vramTotalMb},
                               {u"temperature"_s, c.temperature},
                               {u"clockMhz"_s, c.clockMhz},
                               {u"asleep"_s, c.asleep}});
    return out;
}

} // namespace jarvis
