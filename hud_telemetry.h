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
#include "graphics_info.h"
// Live numbers for the HUD panels: CPU, memory, temperature, the GPU and the
// interface's own frame rate. Measured only while `active` (a panel shows them),
// once a second; every GPU found every 2 s, through sysfs (AMD, Intel) or
// nvidia-smi — which runs only while the NVIDIA card is awake, so watching it
// never wakes a sleeping card. gpu/gpuName are the card that renders Jarvis
// (GraphicsInfo decides), `gpus` all of them.
// Values that cannot be measured stay at -1 — the HUD shows "—", never a guess.
class HudTelemetry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY activeChanged)
    Q_PROPERTY(QObject *graphics READ graphics WRITE setGraphics NOTIFY activeChanged)
    Q_PROPERTY(double cpu READ cpu NOTIFY updated)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(double ram READ ram NOTIFY updated)
    Q_PROPERTY(double ramUsedGb READ ramUsedGb NOTIFY updated)
    Q_PROPERTY(double ramTotalGb READ ramTotalGb NOTIFY updated)
    Q_PROPERTY(double temperature READ temperature NOTIFY updated)
    Q_PROPERTY(double gpu READ gpu NOTIFY updated)
    Q_PROPERTY(double gpuMemory READ gpuMemory NOTIFY updated)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY updated)
    Q_PROPERTY(QVariantList gpus READ gpus NOTIFY updated)
    Q_PROPERTY(int cores READ cores CONSTANT)
    Q_PROPERTY(double fps READ fps NOTIFY updated)
public:
    explicit HudTelemetry(QObject *parent=nullptr);
    ~HudTelemetry() override;
    bool active() const { return m_timer.isActive(); }
    void setActive(bool active);
    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    QObject *graphics() const { return m_graphics; }
    void setGraphics(QObject *graphics);
    double cpu() const { return m_cpu; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    double ram() const { return m_ram; }
    double ramUsedGb() const { return m_ramUsed; }
    double ramTotalGb() const { return m_ramTotal; }
    double temperature() const { return m_temp; }
    double gpu() const;
    double gpuMemory() const;
    QString gpuName() const;
    QVariantList gpus() const;
    int cores() const;
    double fps() const { return m_fps; }
signals:
    void activeChanged();
    void updated();
private:
    void sample();
    void sampleGpu();
    int shownGpu() const;   // the rendering card, else the first one measured
    struct Card { QString name; QString slot; double load=-1,memory=-1; bool asleep=false,nvidia=false; QString busyFile,powerFile; };
    QTimer m_timer;
    QProcess m_nvidia;
    QPointer<QQuickWindow> m_window;
    QPointer<GraphicsInfo> m_graphics;
    QList<Card> m_cards;
    QMetaObject::Connection m_frames;
    jv_cpu_times m_prev{};
    bool m_havePrev=false;
    std::atomic<int> m_frameCount{0}; // counted on the render thread
    int m_ticks=0;
    bool m_noNvidia=false;
    double m_cpu=-1,m_ram=-1,m_ramUsed=-1,m_ramTotal=-1,m_temp=-1,m_fps=-1;
    QVariantList m_cpuHistory;
};
