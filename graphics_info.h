#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStringList>
#include <QVariantList>
#include <atomic>
// Which GPU and graphics API really draw Jarvis — measured, not assumed.
//
// Before QApplication exists, prepareEnvironment() applies the user's choice
// (Control Center → Application → Interface): the graphics card, through PRIME
// render offload on hybrid laptops (NVIDIA's offload variables, or DRI_PRIME for
// Mesa drivers), and after it applyApi() picks Qt's RHI backend (OpenGL or
// Vulkan). The variables are set for this process only and, with OpenGL, dropped
// again after the first frame, so programs Jarvis opens keep the system default.
// A choice that keeps Jarvis from drawing is undone at the next start.
//
// After the first frame of a watched window, the object reports what renders:
// the API, the driver's renderer string and whether it is a software rasterizer,
// next to the GPUs found in /sys/class/drm. Finding an NVIDIA card proves
// nothing; the renderer string does.
class GraphicsInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString api READ api NOTIFY changed)
    Q_PROPERTY(QString apiVersion READ apiVersion NOTIFY changed)
    Q_PROPERTY(QString renderer READ renderer NOTIFY changed)
    Q_PROPERTY(QString vendor READ vendor NOTIFY changed)
    Q_PROPERTY(bool software READ software NOTIFY changed)
    Q_PROPERTY(bool shadersSupported READ shadersSupported CONSTANT)
    Q_PROPERTY(QVariantList gpus READ gpus NOTIFY changed)
    Q_PROPERTY(int renderingGpu READ renderingGpu NOTIFY changed)
    Q_PROPERTY(QString renderingName READ renderingName NOTIFY changed)
    Q_PROPERTY(QString gpuChoice READ gpuChoice WRITE setGpuChoice NOTIFY choiceChanged)
    Q_PROPERTY(QString apiChoice READ apiChoice WRITE setApiChoice NOTIFY choiceChanged)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY choiceChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    // Of the screen the first watched window is on; 0 unknown.
    Q_PROPERTY(double refreshRate READ refreshRate NOTIFY refreshRateChanged)
public:
    struct Gpu {
        QString card;       // card0
        QString slot;       // PCI address, 0000:01:00.0
        QString vendorId;   // 10de
        QString deviceId;
        QString name;       // NVIDIA GeForce RTX 3050 Mobile
        QString driver;     // nvidia, amdgpu, i915, ...
        QString devicePath; // /sys/class/drm/card0/device
        bool boot=false;    // the GPU the firmware (and usually the desktop) starts on
    };
    static QList<Gpu> detectGpus();
    // Before QApplication: the choice from the settings (or --gpu / --graphics-api).
    static void prepareEnvironment(int argc, char **argv);
    // After QApplication, before the first window.
    static void applyApi();
    // This process is the Jarvis that will draw: a non-default choice is on trial
    // until its first frame.
    static void armProbe();
    // jarvis --gpu-info: what this process renders with, printed to stdout.
    static int printReport();

    explicit GraphicsInfo(QObject *parent=nullptr);
    // Measures the first frame of a window; the first window to draw decides.
    void watch(QQuickWindow *window);

    bool ready() const { return m_ready; }
    QString api() const { return m_api; }
    QString apiVersion() const { return m_apiVersion; }
    QString renderer() const { return m_renderer; }
    QString vendor() const { return m_vendor; }
    bool software() const { return m_software; }
    bool shadersSupported() const;
    QVariantList gpus() const;
    QList<Gpu> gpuList() const { return m_gpus; }
    int renderingGpu() const { return m_renderingGpu; }
    QString renderingName() const;
    QString gpuChoice() const;
    void setGpuChoice(const QString &choice);
    QString apiChoice() const;
    void setApiChoice(const QString &choice);
    bool restartNeeded() const;
    QString notice() const;
    QStringList warnings() const;
    double refreshRate() const { return m_refreshRate; }

    // The compact overlay needs an alpha channel; the main window must not have one
    // (an ARGB window makes the compositor blend it over the desktop every frame).
    Q_INVOKABLE void makeTranslucent(QQuickWindow *window) const;
    Q_INVOKABLE QString report() const;
    Q_INVOKABLE void copyReport() const;
signals:
    void changed();
    void choiceChanged();
    void refreshRateChanged();
    // The chosen card or API cannot draw; Jarvis must start again to use the defaults.
    void restartRequested();
private:
    void measured(const QString &api, const QString &version, const QString &vendor, const QString &renderer, uint vendorId, bool cpuDevice);
    void failed(QQuickWindow::SceneGraphError error, const QString &message);
    void clearProbe();
    QList<Gpu> m_gpus;
    std::atomic<bool> m_measuring{false};
    bool m_ready=false,m_software=false;
    QString m_api,m_apiVersion,m_renderer,m_vendor;
    int m_renderingGpu=-1;
    double m_refreshRate=0;
    QPointer<QQuickWindow> m_screenWindow;
    QMetaObject::Connection m_screenRate;
    void followScreen(QScreen *screen);
};
