#include "hud_telemetry.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>

HudTelemetry::HudTelemetry(QObject *parent):QObject(parent) {
    m_timer.setInterval(1000);
    connect(&m_timer,&QTimer::timeout,this,&HudTelemetry::sample);
    // AMD and Intel report the GPU load in sysfs; NVIDIA's driver does not.
    for(const QString &card:QDir("/sys/class/drm").entryList({"card?"},QDir::Dirs|QDir::NoDotAndDotDot|QDir::System))
        if(const QString f="/sys/class/drm/"+card+"/device/gpu_busy_percent";QFile::exists(f)) {m_gpuBusyFile=f;break;}
    m_noNvidia=QStandardPaths::findExecutable("nvidia-smi").isEmpty();
    connect(&m_nvidia,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        // "NVIDIA GeForce RTX 3050 Laptop GPU, 12, 1043, 6144"
        const QStringList f=QString::fromUtf8(m_nvidia.readAllStandardOutput()).split('\n').value(0).split(',');
        if(code!=0 || f.size()<4) {m_noNvidia=true;return;}
        m_gpuName=f.at(0).trimmed().left(60);
        m_gpu=f.at(1).trimmed().toDouble();
        const double total=f.at(3).trimmed().toDouble();
        m_gpuMem=total>0 ? 100.0*f.at(2).trimmed().toDouble()/total : -1;
        emit updated();
    });
}
HudTelemetry::~HudTelemetry() {
    if(m_nvidia.state()!=QProcess::NotRunning){m_nvidia.kill();m_nvidia.waitForFinished(500);}
}
void HudTelemetry::setActive(bool active) {
    if(active==m_timer.isActive()) return;
    if(active) {m_havePrev=false;m_frameCount=0;sample();m_timer.start();}
    else m_timer.stop();
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
    if(++m_ticks%2==1) sampleGpu();
    emit updated();
}
void HudTelemetry::sampleGpu() {
    if(!m_gpuBusyFile.isEmpty()) {
        QFile f(m_gpuBusyFile);
        if(f.open(QIODevice::ReadOnly)) m_gpu=f.readAll().trimmed().toDouble();
        return;
    }
    if(m_noNvidia || m_nvidia.state()!=QProcess::NotRunning) return;
    m_nvidia.start("nvidia-smi",{"--query-gpu=name,utilization.gpu,memory.used,memory.total","--format=csv,noheader,nounits"});
}
