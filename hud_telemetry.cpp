#include "hud_telemetry.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>

HudTelemetry::HudTelemetry(QObject *parent):QObject(parent) {
    m_timer.setInterval(1000);
    connect(&m_timer,&QTimer::timeout,this,&HudTelemetry::sample);
    connect(&m_gpus,&jarvis::GpuMonitor::updated,this,&HudTelemetry::updated);
}
HudTelemetry::~HudTelemetry()=default;
void HudTelemetry::setActive(bool active) {
    if(active==m_timer.isActive()) return;
    if(active) {m_havePrev=false;m_frameCount=0;sample();m_timer.start();}
    else m_timer.stop();
    emit activeChanged();
}
void HudTelemetry::setGraphics(QObject *graphics) {
    if(graphics==m_graphics) return;
    m_graphics=qobject_cast<GraphicsInfo *>(graphics);
    if(m_graphics) connect(m_graphics,&GraphicsInfo::changed,this,&HudTelemetry::updated);
    emit activeChanged();
}
void HudTelemetry::setWindow(QQuickWindow *window) {
    if(window==m_window) return;
    disconnect(m_frames);
    m_window=window;
    if(m_window) m_frames=connect(m_window,&QQuickWindow::frameSwapped,this,[this]{++m_frameCount;},Qt::DirectConnection);
    emit activeChanged();
}
int HudTelemetry::cores() const { return jv_cpu_count(); }
void HudTelemetry::sample() {
    jv_cpu_times now{};
    if(jv_cpu_times_read(&now)==0) {
        if(m_havePrev) {
            m_cpu=jv_cpu_usage_between(&m_prev,&now);
            m_cpuHistory.append(m_cpu);
            while(m_cpuHistory.size()>48) m_cpuHistory.removeFirst();
        }
        m_prev=now;m_havePrev=true;
    }
    jv_meminfo mem{};
    if(jv_mem_read(&mem)==0 && mem.total_kb>0) {
        m_ramTotal=double(mem.total_kb)/1048576.0;
        m_ramUsed=double(mem.total_kb-mem.available_kb)/1048576.0;
        m_ram=100.0*m_ramUsed/m_ramTotal;
    }
    double celsius=0;
    m_temp=jv_thermal_max_c(&celsius)==0 ? celsius : -1;
    // Frames the window really drew in the last second (frameSwapped may come from the render thread).
    m_fps=m_window ? double(m_frameCount.exchange(0)) : -1;
    if(++m_ticks%2==1) m_gpus.sample();
    emit updated();
}
int HudTelemetry::shownGpu() const {
    const auto &cards=m_gpus.cards();
    if(m_graphics && m_graphics->renderingGpu()>=0 && m_graphics->renderingGpu()<cards.size()) return m_graphics->renderingGpu();
    for(int i=0;i<cards.size();++i) if(cards.at(i).load>=0) return i;
    return -1;
}
double HudTelemetry::gpu() const { const int i=shownGpu(); return i>=0 ? m_gpus.cards().at(i).load : -1; }
double HudTelemetry::gpuMemory() const { const int i=shownGpu(); return i>=0 ? m_gpus.cards().at(i).memory : -1; }
QString HudTelemetry::gpuName() const { const int i=shownGpu(); return i>=0 ? m_gpus.cards().at(i).name : QString(); }
QVariantList HudTelemetry::gpus() const {
    QVariantList list;
    const int rendering=m_graphics ? m_graphics->renderingGpu() : -1;
    for(int i=0;i<m_gpus.cards().size();++i) {
        const auto &c=m_gpus.cards().at(i);
        list.append(QVariantMap{{QStringLiteral("name"),c.name},{QStringLiteral("load"),c.load},{QStringLiteral("memory"),c.memory},
                                {QStringLiteral("asleep"),c.asleep},{QStringLiteral("rendering"),i==rendering}});
    }
    return list;
}
