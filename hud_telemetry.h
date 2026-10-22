#pragma once
#include <QObject>
#include <atomic>
#include <QPointer>
#include <QProcess>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include "jv_sys.h"
// Live numbers for the HUD panels: CPU, memory, temperature, the GPU and the
// interface's own frame rate. Measured only while `active` (a panel shows them),
// once a second; the GPU through sysfs (AMD, Intel) or nvidia-smi every 2 s.
// Values that cannot be measured stay at -1 — the HUD shows "—", never a guess.
class HudTelemetry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY activeChanged)
    Q_PROPERTY(double cpu READ cpu NOTIFY updated)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(double ram READ ram NOTIFY updated)
    Q_PROPERTY(double ramUsedGb READ ramUsedGb NOTIFY updated)
    Q_PROPERTY(double ramTotalGb READ ramTotalGb NOTIFY updated)
    Q_PROPERTY(double temperature READ temperature NOTIFY updated)
    Q_PROPERTY(double gpu READ gpu NOTIFY updated)
    Q_PROPERTY(double gpuMemory READ gpuMemory NOTIFY updated)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY updated)
    Q_PROPERTY(int cores READ cores CONSTANT)
    Q_PROPERTY(double fps READ fps NOTIFY updated)
public:
    explicit HudTelemetry(QObject *parent=nullptr);
    ~HudTelemetry() override;
    bool active() const { return m_timer.isActive(); }
    void setActive(bool active);
    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    double cpu() const { return m_cpu; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    double ram() const { return m_ram; }
    double ramUsedGb() const { return m_ramUsed; }
    double ramTotalGb() const { return m_ramTotal; }
    double temperature() const { return m_temp; }
    double gpu() const { return m_gpu; }
    double gpuMemory() const { return m_gpuMem; }
    QString gpuName() const { return m_gpuName; }
    int cores() const;
    double fps() const { return m_fps; }
signals:
    void activeChanged();
    void updated();
private:
    void sample();
    void sampleGpu();
    QTimer m_timer;
    QProcess m_nvidia;
    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_frames;
    jv_cpu_times m_prev{};
    bool m_havePrev=false;
    std::atomic<int> m_frameCount{0}; // counted on the render thread
    int m_ticks=0;
    QString m_gpuBusyFile,m_gpuName;
    bool m_noNvidia=false;
    double m_cpu=-1,m_ram=-1,m_ramUsed=-1,m_ramTotal=-1,m_temp=-1,m_gpu=-1,m_gpuMem=-1,m_fps=-1;
    QVariantList m_cpuHistory;
};
