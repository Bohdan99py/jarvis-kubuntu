#pragma once

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>

namespace jarvis {

// A graphics card as /sys/class/drm describes it (shared by the window's
// graphics diagnostics and jarvisd's telemetry for the desktop widgets).
struct GpuDevice
{
    QString card;       // card0
    QString slot;       // PCI address, 0000:01:00.0
    QString vendorId;   // 10de
    QString deviceId;
    QString name;       // NVIDIA GeForce RTX 3050 Mobile
    QString driver;     // nvidia, amdgpu, i915, ...
    QString devicePath; // /sys/class/drm/card0/device
    bool boot = false;  // the GPU the firmware (and usually the desktop) starts on
};

// JARVIS_SYSFS_ROOT replaces /sys (tests with fake hybrid laptops).
QList<GpuDevice> detectGpus();
QString gpuVendorName(const QString &vendorId);

// Live numbers of every GPU: load, VRAM, temperature and core clock — through
// sysfs for AMD and Intel, through nvidia-smi for NVIDIA, which is asked only
// while the card is awake (watching never wakes a runtime-suspended card).
// sample() is cheap and never blocks: nvidia-smi runs asynchronously and its
// answer arrives with updated(). Unknown values are -1.
class GpuMonitor : public QObject
{
    Q_OBJECT

public:
    struct Card
    {
        GpuDevice device;
        QString name;
        double load = -1;          // percent
        double memory = -1;        // VRAM used, percent
        double vramUsedMb = -1;
        double vramTotalMb = -1;
        double temperature = -1;   // °C
        double clockMhz = -1;      // graphics clock
        bool asleep = false;       // runtime-suspended
    };

    explicit GpuMonitor(QObject *parent = nullptr);
    ~GpuMonitor() override;

    void sample();
    const QList<Card> &cards() const { return m_cards; }
    // [{name, driver, slot, load, memory, vramUsedMb, vramTotalMb, temperature, clockMhz, asleep, boot}]
    QJsonArray json() const;

signals:
    void updated();

private:
    QList<Card> m_cards;
    QProcess m_nvidia;
    bool m_noNvidia = false;
};

} // namespace jarvis
