#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "jv_sys.h"

namespace jarvis {

// Qt-side adapter over the pure-C jv_sys layer. Produces human-readable
// reports (RU/EN). QtCore only, so it can live inside a headless daemon.
class SystemInfo : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SystemInfo)

public:
    explicit SystemInfo(QObject *parent = nullptr);
    ~SystemInfo() override = default;

    QString cpuReport(bool ru) const;
    QString memoryReport(bool ru) const;
    QString batteryReport(bool ru) const;
    QString temperatureReport(bool ru) const;
    QString uptimeReport(bool ru) const;
    QString diskReport(bool ru) const;
    QString systemReport(bool ru) const;
    QString processesReport(bool ru) const;

private:
    void sample();

    // CPU usage needs two snapshots; a light timer keeps a 2 s window ready.
    QTimer m_timer;
    jv_cpu_times m_prev{};
    jv_cpu_times m_last{};
    int m_samples = 0;
};

} // namespace jarvis
