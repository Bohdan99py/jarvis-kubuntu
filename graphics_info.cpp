#include "graphics_info.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QVersionNumber>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#endif
#include <algorithm>
#include <memory>
#include <utility>
#include "build_config.h"

namespace {
// What this process started with; set before QApplication, read by the object later.
struct Applied {
    QString gpu=QStringLiteral("auto"),api=QStringLiteral("auto");
    QStringList setVars;    // variables Jarvis set for itself
    QStringList clearedVars;
    // Found before QApplication (no translator yet): kept as facts, worded when shown.
    QStringList environment;  // VAR=value forcing a renderer
    QString undone;           // "card/api" undone because it never drew a frame
    bool noSecondCard=false;
    QStringList warnings;     // worded already (after QApplication)
    QString notice;           // from the settings or a failure
};
Applied &applied() { static Applied a; return a; }

// The application's settings file, also before QApplication has its names.
QSettings *openSettings() {
    return new QSettings(QSettings::NativeFormat,QSettings::UserScope,QStringLiteral("Jarvis"),QStringLiteral("jarvis"));
}

const char *const offloadVars[]={"__NV_PRIME_RENDER_OFFLOAD","__GLX_VENDOR_LIBRARY_NAME","__VK_LAYER_NV_optimus","DRI_PRIME"};

QString readText(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.read(256)).trimmed() : QString();
}
// The GPU that renders by default: the boot GPU, else the first one found.
int defaultIndex(const QList<GraphicsInfo::Gpu> &gpus) {
    for(int i=0;i<gpus.size();++i) if(gpus.at(i).boot) return i;
    return gpus.isEmpty() ? -1 : 0;
}
// The card a choice means: "integrated" the default one, "dedicated" another one
// (NVIDIA first, then AMD), -1 when there is none.
int chosenIndex(const QList<GraphicsInfo::Gpu> &gpus,const QString &choice) {
    const int def=defaultIndex(gpus);
    if(choice==u"integrated") return def;
    if(choice!=u"dedicated") return -1;
    int best=-1;
    for(int i=0;i<gpus.size();++i) {
        if(i==def) continue;
        if(best<0 || gpus.at(i).vendorId==u"10de" || (gpus.at(i).vendorId==u"1002" && gpus.at(best).vendorId!=u"10de")) best=i;
    }
    return best;
}
// Whether a Jarvis process with this pid is alive (pids get reused).
bool jarvisRunning(qint64 pid) {
    if(pid<=0 || pid==QCoreApplication::applicationPid()) return false;
    return readText(QStringLiteral("/proc/%1/comm").arg(pid))==u"jarvis";
}
bool isSoftwareName(const QString &text) {
    static const char *const names[]={"llvmpipe","softpipe","lavapipe","swrast","software rasterizer","swiftshader","basic render"};
    const QString lower=text.toLower();
    return std::any_of(std::begin(names),std::end(names),[&](const char *n){return lower.contains(QLatin1String(n));});
}
// The PCI vendor a driver's strings name.
QString vendorFromText(const QString &text) {
    const QString t=text.toLower();
    if(t.contains(u"nvidia") || t.contains(u"geforce") || t.contains(u"quadro") || t.contains(u"nouveau")) return QStringLiteral("10de");
    if(t.contains(u"amd") || t.contains(u"radeon") || t.contains(u"ati technologies")) return QStringLiteral("1002");
    if(t.contains(u"intel")) return QStringLiteral("8086");
    return {};
}
} // namespace

void GraphicsInfo::prepareEnvironment(int argc,char **argv) {
    Applied &a=applied();
    // A Jarvis that restarts itself passes on the variables it set for itself; they
    // are not the user's, and the new choice may differ.
    for(const QString &var:qEnvironmentVariable("JARVIS_GRAPHICS_SET").split(u',',Qt::SkipEmptyParts)) qunsetenv(var.toLatin1().constData());
    qunsetenv("JARVIS_GRAPHICS_SET");
    std::unique_ptr<QSettings> s(openSettings());
    a.gpu=s->value(QStringLiteral("graphics/gpu"),QStringLiteral("auto")).toString();
    a.api=s->value(QStringLiteral("graphics/api"),QStringLiteral("auto")).toString();
    bool fromCommandLine=false;
    for(int i=1;i+1<argc;++i) {
        if(qstrcmp(argv[i],"--gpu")==0) {a.gpu=QString::fromLocal8Bit(argv[i+1]);fromCommandLine=true;}
        if(qstrcmp(argv[i],"--graphics-api")==0) {a.api=QString::fromLocal8Bit(argv[i+1]);fromCommandLine=true;}
    }
    if(!QStringList{QStringLiteral("auto"),QStringLiteral("integrated"),QStringLiteral("dedicated")}.contains(a.gpu)) a.gpu=QStringLiteral("auto");
    if(!QStringList{QStringLiteral("auto"),QStringLiteral("opengl"),QStringLiteral("vulkan")}.contains(a.api)) a.api=QStringLiteral("auto");
    a.notice=s->value(QStringLiteral("graphics/notice")).toString();
    // A choice that kept Jarvis from drawing its window last time is undone. The
    // instance that renders arms a marker "card/api@pid" (armProbe) and removes it
    // after its first frame or at a clean exit; a marker whose process is gone
    // means that start never got that far. Another Jarvis still running (a tray
    // instance that has not drawn yet) is no failure.
    if(!fromCommandLine && (a.gpu!=u"auto" || a.api!=u"auto")) {
        const QString attempt=a.gpu+u'/'+a.api;
        const QString probe=s->value(QStringLiteral("graphics/probe")).toString();
        if(probe.section(u'@',0,0)==attempt && !jarvisRunning(probe.section(u'@',1).toLongLong())) {
            a.undone=attempt;
            s->setValue(QStringLiteral("graphics/gpu"),QStringLiteral("auto"));
            s->setValue(QStringLiteral("graphics/api"),QStringLiteral("auto"));
            s->remove(QStringLiteral("graphics/notice"));
            s->remove(QStringLiteral("graphics/probe"));
            a.gpu=a.api=QStringLiteral("auto");
        }
    }
    // The environment can force a software renderer; say so instead of guessing.
    const auto env=[&](const char *name,auto forces) {
        const QString value=qEnvironmentVariable(name);
        if(!value.isEmpty() && forces(value.toLower())) a.environment<<QStringLiteral("%1=%2").arg(QLatin1String(name),value);
    };
    env("QT_QUICK_BACKEND",[](const QString &v){return v.startsWith(u"software");});
    env("QSG_RHI_BACKEND",[](const QString &){return true;});
    env("QSG_RHI_PREFER_SOFTWARE_RENDERER",[](const QString &v){return v!=u"0";});
    env("LIBGL_ALWAYS_SOFTWARE",[](const QString &v){return v!=u"0" && v!=u"false";});
    env("GALLIUM_DRIVER",[](const QString &v){return v==u"llvmpipe" || v==u"softpipe";});
    env("QT_XCB_GL_INTEGRATION",[](const QString &v){return v==u"none";});
    if(a.gpu==u"auto") return;
    const QList<Gpu> gpus=detectGpus();
    const int target=chosenIndex(gpus,a.gpu);
    if(target<0) {
        a.noSecondCard=true;
        return;
    }
    if(target==defaultIndex(gpus)) {
        // The default card: undo an offload inherited from whatever started Jarvis.
        for(const char *var:offloadVars) if(qEnvironmentVariableIsSet(var)) {qunsetenv(var);a.clearedVars<<QLatin1String(var);}
        return;
    }
    const auto put=[&](const char *name,const QByteArray &value){qputenv(name,value);a.setVars<<QLatin1String(name);};
    const Gpu &g=gpus.at(target);
    if(g.driver==u"nvidia") {
        // NVIDIA's PRIME render offload: GLX, EGL (Wayland) and Vulkan.
        put("__NV_PRIME_RENDER_OFFLOAD","1");
        put("__GLX_VENDOR_LIBRARY_NAME","nvidia");
        put("__VK_LAYER_NV_optimus","NVIDIA_only");
    } else {
        // Mesa drivers (amdgpu, i915, nouveau…): this exact device, for OpenGL and Vulkan.
        put("DRI_PRIME","pci-"+QString(g.slot).replace(u':',u'_').replace(u'.',u'_').toLatin1());
    }
    qputenv("JARVIS_GRAPHICS_SET",a.setVars.join(u',').toLatin1());
}

void GraphicsInfo::armProbe() {
    const Applied &a=applied();
    if(a.gpu==u"auto" && a.api==u"auto") return;
    std::unique_ptr<QSettings> s(openSettings());
    s->setValue(QStringLiteral("graphics/probe"),QStringLiteral("%1/%2@%3").arg(a.gpu,a.api).arg(QCoreApplication::applicationPid()));
}

void GraphicsInfo::applyApi() {
    Applied &a=applied();
    if(a.api==u"vulkan") {
        // Qt 6.4 crashes instead of reporting an error when Vulkan cannot start,
        // so it gets Vulkan only after an instance and a device were found here.
        QString missing;
#if QT_CONFIG(vulkan)
        QVulkanInstance probe;
        probe.setApiVersion(QVersionNumber(1,0));
        uint32_t devices=0;
        if(!probe.create()) missing=QCoreApplication::translate("GraphicsInfo","no Vulkan driver (error %1)").arg(int(probe.errorCode()));
        else if(probe.functions()->vkEnumeratePhysicalDevices(probe.vkInstance(),&devices,nullptr)!=VK_SUCCESS || devices==0)
            missing=QCoreApplication::translate("GraphicsInfo","no Vulkan device");
#else
        missing=QCoreApplication::translate("GraphicsInfo","this Qt has no Vulkan support");
#endif
        if(missing.isEmpty()) {QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);return;}
        // The choice stays (a driver may come later); this start draws with OpenGL.
        a.warnings<<QCoreApplication::translate("GraphicsInfo","Vulkan cannot be used (%1); Jarvis draws with OpenGL.").arg(missing);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return;
    }
    if(a.api==u"opengl") QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
}

GraphicsInfo::GraphicsInfo(QObject *parent):QObject(parent),m_gpus(detectGpus()) {
    // A clean exit is no sign of a broken choice, even if no window was ever shown.
    connect(qApp,&QCoreApplication::aboutToQuit,this,&GraphicsInfo::clearProbe);
}

void GraphicsInfo::followScreen(QScreen *screen) {
    disconnect(m_screenRate);
    const auto update=[this,screen]{
        const double rate=screen ? screen->refreshRate() : 0;
        if(qFuzzyCompare(rate+1,m_refreshRate+1)) return;
        m_refreshRate=rate;
        emit refreshRateChanged();
    };
    if(screen) m_screenRate=connect(screen,&QScreen::refreshRateChanged,this,update);
    update();
}

void GraphicsInfo::watch(QQuickWindow *window) {
    if(!window) return;
    // The first window watched is the main one: its screen's refresh rate paces the HUD.
    if(!m_screenWindow) {
        m_screenWindow=window;
        connect(window,&QWindow::screenChanged,this,&GraphicsInfo::followScreen);
        followScreen(window->screen());
    }
    connect(window,&QQuickWindow::sceneGraphError,this,&GraphicsInfo::failed,Qt::QueuedConnection);
    // On the render thread, with the window's graphics context current.
    connect(window,&QQuickWindow::beforeRendering,this,[this,window]{
        if(m_measuring.exchange(true)) return;
        QSGRendererInterface *ri=window->rendererInterface();
        QString api,version,vendor,renderer;
        uint vendorId=0;
        bool cpu=false;
        switch(ri ? ri->graphicsApi() : QSGRendererInterface::Unknown) {
        case QSGRendererInterface::OpenGL:
            api=QStringLiteral("OpenGL");
            if(QOpenGLContext *ctx=QOpenGLContext::currentContext()) {
                QOpenGLFunctions *f=ctx->functions();
                const auto text=[f](GLenum name){
                    const GLubyte *s=f->glGetString(name);
                    return s ? QString::fromUtf8(reinterpret_cast<const char *>(s)) : QString();
                };
                vendor=text(GL_VENDOR);renderer=text(GL_RENDERER);version=text(GL_VERSION);
                if(ctx->isOpenGLES()) api=QStringLiteral("OpenGL ES");
            }
            break;
        case QSGRendererInterface::Vulkan:
            api=QStringLiteral("Vulkan");
#if QT_CONFIG(vulkan)
            if(auto *device=static_cast<VkPhysicalDevice *>(ri->getResource(window,QSGRendererInterface::PhysicalDeviceResource)))
                if(auto *instance=static_cast<QVulkanInstance *>(ri->getResource(window,QSGRendererInterface::VulkanInstanceResource)); instance && *device) {
                    VkPhysicalDeviceProperties p{};
                    instance->functions()->vkGetPhysicalDeviceProperties(*device,&p);
                    renderer=QString::fromUtf8(p.deviceName);
                    vendorId=p.vendorID;
                    cpu=p.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU;
                    vendor=jarvis::gpuVendorName(QStringLiteral("%1").arg(p.vendorID,4,16,QLatin1Char('0')));
                    version=QStringLiteral("%1.%2.%3").arg(VK_API_VERSION_MAJOR(p.apiVersion)).arg(VK_API_VERSION_MINOR(p.apiVersion)).arg(VK_API_VERSION_PATCH(p.apiVersion));
                }
#endif
            break;
        case QSGRendererInterface::Software:
            api=QStringLiteral("Software");
            renderer=QStringLiteral("Qt Quick software renderer");
            cpu=true;
            break;
        default:
            api=QStringLiteral("other (%1)").arg(int(ri ? ri->graphicsApi() : QSGRendererInterface::Unknown));
        }
        QMetaObject::invokeMethod(this,[=]{measured(api,version,vendor,renderer,vendorId,cpu);},Qt::QueuedConnection);
    },Qt::DirectConnection);
}

void GraphicsInfo::measured(const QString &api,const QString &version,const QString &vendor,const QString &renderer,uint vendorId,bool cpuDevice) {
    m_api=api;m_apiVersion=version;m_vendor=vendor;m_renderer=renderer;
    m_software=cpuDevice || isSoftwareName(vendor+u' '+renderer);
    // The detected GPU that renders: by PCI vendor (Vulkan reports it, OpenGL names it).
    m_renderingGpu=-1;
    const QString vid=vendorId ? QStringLiteral("%1").arg(vendorId,4,16,QLatin1Char('0')) : vendorFromText(vendor+u' '+renderer);
    if(!m_software && !vid.isEmpty()) {
        const QString text=(vendor+u' '+renderer).toLower();
        for(int i=0;i<m_gpus.size();++i) {
            if(m_gpus.at(i).vendorId!=vid) continue;
            if(m_renderingGpu<0) m_renderingGpu=i;
            // Two cards of one vendor: the one whose model the renderer names.
            if(text.contains(m_gpus.at(i).name.section(u' ',-1).toLower())) {m_renderingGpu=i;break;}
        }
    }
    m_ready=true;
    clearProbe();   // it draws: the choice works
    // From now on programs Jarvis opens keep the system's default GPU: OpenGL has
    // bound its device. Vulkan may enumerate devices again for a later window.
    Applied &a=applied();
    if(!api.startsWith(u"Vulkan") && !a.setVars.isEmpty()) {
        for(const QString &var:std::as_const(a.setVars)) qunsetenv(var.toLatin1().constData());
        qunsetenv("JARVIS_GRAPHICS_SET");
    }
    QString gpus;
    for(int i=0;i<m_gpus.size();++i)
        gpus+=QStringLiteral("%1%2 %3 (%4%5)").arg(i?QStringLiteral(", "):QString(),m_gpus.at(i).card,m_gpus.at(i).name,m_gpus.at(i).driver,m_gpus.at(i).boot?QStringLiteral(", boot"):QString());
    qInfo().noquote()<<QStringLiteral("jarvis: graphics: %1 %2 — renderer: %3 — vendor: %4 — %5 — rendering GPU: %6 — GPUs found: %7 — choice: card %8, API %9")
                         .arg(api,version,renderer,vendor,m_software?QStringLiteral("SOFTWARE rendering on the CPU"):QStringLiteral("hardware"),
                              m_renderingGpu>=0?m_gpus.at(m_renderingGpu).card:QStringLiteral("unknown"),gpus.isEmpty()?QStringLiteral("none"):gpus,a.gpu,a.api);
    emit changed();
}

void GraphicsInfo::clearProbe() {
    std::unique_ptr<QSettings> s(openSettings());
    if(s->value(QStringLiteral("graphics/probe")).toString().section(u'@',1).toLongLong()==QCoreApplication::applicationPid())
        s->remove(QStringLiteral("graphics/probe"));
}

void GraphicsInfo::failed(QQuickWindow::SceneGraphError error,const QString &message) {
    qCritical().noquote()<<"jarvis: graphics: the scene graph could not start (error"<<int(error)<<"):"<<message;
    Applied &a=applied();
    std::unique_ptr<QSettings> s(openSettings());
    if(a.gpu!=u"auto" || a.api!=u"auto") {
        a.notice=tr("The graphics settings (card: %1, API: %2) failed: %3. Jarvis restarted with the system defaults.").arg(a.gpu,a.api,message.left(300));
        s->setValue(QStringLiteral("graphics/gpu"),QStringLiteral("auto"));
        s->setValue(QStringLiteral("graphics/api"),QStringLiteral("auto"));
        s->setValue(QStringLiteral("graphics/notice"),a.notice);
        s->remove(QStringLiteral("graphics/probe"));
        // This process cannot change its graphics device any more: start again.
        emit restartRequested();
        return;
    }
    a.notice=tr("The graphics driver could not start the interface: %1").arg(message.left(300));
    emit changed();
}

bool GraphicsInfo::shadersSupported() const {
    return QSGRendererInterface::isApiRhiBased(QQuickWindow::graphicsApi());
}

QVariantList GraphicsInfo::gpus() const {
    QVariantList list;
    for(int i=0;i<m_gpus.size();++i) {
        const Gpu &g=m_gpus.at(i);
        list.append(QVariantMap{{QStringLiteral("card"),g.card},{QStringLiteral("name"),g.name},{QStringLiteral("driver"),g.driver},
                                {QStringLiteral("slot"),g.slot},{QStringLiteral("boot"),g.boot},{QStringLiteral("rendering"),i==m_renderingGpu}});
    }
    return list;
}

QString GraphicsInfo::renderingName() const {
    if(!m_ready) return {};
    if(m_software) return tr("CPU (software rendering)");
    return m_renderingGpu>=0 ? m_gpus.at(m_renderingGpu).name : m_renderer;
}

QString GraphicsInfo::gpuChoice() const {
    std::unique_ptr<QSettings> s(openSettings());
    return s->value(QStringLiteral("graphics/gpu"),QStringLiteral("auto")).toString();
}
void GraphicsInfo::setGpuChoice(const QString &choice) {
    if(!QStringList{QStringLiteral("auto"),QStringLiteral("integrated"),QStringLiteral("dedicated")}.contains(choice) || choice==gpuChoice()) return;
    std::unique_ptr<QSettings> s(openSettings());
    s->setValue(QStringLiteral("graphics/gpu"),choice);
    s->remove(QStringLiteral("graphics/notice"));
    emit choiceChanged();
}
QString GraphicsInfo::apiChoice() const {
    std::unique_ptr<QSettings> s(openSettings());
    return s->value(QStringLiteral("graphics/api"),QStringLiteral("auto")).toString();
}
void GraphicsInfo::setApiChoice(const QString &choice) {
    if(!QStringList{QStringLiteral("auto"),QStringLiteral("opengl"),QStringLiteral("vulkan")}.contains(choice) || choice==apiChoice()) return;
    std::unique_ptr<QSettings> s(openSettings());
    s->setValue(QStringLiteral("graphics/api"),choice);
    s->remove(QStringLiteral("graphics/notice"));
    emit choiceChanged();
}
bool GraphicsInfo::restartNeeded() const {
    return gpuChoice()!=applied().gpu || apiChoice()!=applied().api;
}
QString GraphicsInfo::notice() const {
    const Applied &a=applied();
    if(!a.undone.isEmpty())
        return tr("Jarvis could not draw its window with the graphics settings chosen last time (card: %1, API: %2), so it uses the system defaults again.")
            .arg(a.undone.section(u'/',0,0),a.undone.section(u'/',1));
    return a.notice;
}
QStringList GraphicsInfo::warnings() const {
    const Applied &a=applied();
    QStringList list;
    for(const QString &variable:a.environment) list<<tr("Set in the environment, changes how Qt renders: %1").arg(variable);
    if(a.noSecondCard) list<<tr("No second graphics card was found; Jarvis uses the default one.");
    return list+a.warnings;
}

void GraphicsInfo::makeTranslucent(QQuickWindow *window) const {
    if(!window) return;
    if(window->handle()) qWarning("jarvis: graphics: a window was made translucent after it was created");
    QSurfaceFormat format=window->requestedFormat();
    format.setAlphaBufferSize(8);
    window->setFormat(format);
    window->setColor(Qt::transparent);
}

QString GraphicsInfo::report() const {
    const Applied &a=applied();
    QString out;
    QTextStream t(&out);
    t<<"Jarvis "<<JARVIS_VERSION<<" — graphics\n";
    t<<"Platform: "<<QGuiApplication::platformName()<<", Qt "<<qVersion()<<"\n";
    t<<"Choice: card "<<a.gpu<<", API "<<a.api<<"\n";
    QStringList set;
    for(const QString &var:a.setVars) set<<var+u'='+qEnvironmentVariable(var.toLatin1().constData(),QStringLiteral("(dropped after the first frame)"));
    t<<"Offload variables set by Jarvis: "<<(set.isEmpty()?QStringLiteral("none"):set.join(QStringLiteral(", ")))
     <<(a.clearedVars.isEmpty()?QString():QStringLiteral(" (cleared: %1)").arg(a.clearedVars.join(QStringLiteral(", "))))<<"\n";
    if(m_ready) {
        t<<"Rendering: "<<m_api<<' '<<m_apiVersion<<"\n";
        t<<"Renderer: "<<m_renderer<<" — vendor: "<<m_vendor<<" — "<<(m_software?"SOFTWARE (CPU)":"hardware")<<"\n";
        t<<"Rendering GPU: "<<(m_renderingGpu>=0?m_gpus.at(m_renderingGpu).card+u' '+m_gpus.at(m_renderingGpu).name:QStringLiteral("not identified"))<<"\n";
    } else t<<"Rendering: no frame drawn yet\n";
    t<<"GPUs found:\n";
    if(m_gpus.isEmpty()) t<<"  none in /sys/class/drm\n";
    for(int i=0;i<m_gpus.size();++i) {
        const Gpu &g=m_gpus.at(i);
        t<<"  "<<g.card<<' '<<g.name<<" — "<<(g.driver.isEmpty()?QStringLiteral("no driver"):g.driver)<<", "<<g.slot
         <<(g.boot?", boot":"")<<(i==m_renderingGpu?"  ← renders Jarvis":"")<<"\n";
    }
    for(const QString &w:warnings()) t<<"Warning: "<<w<<"\n";
    if(!notice().isEmpty()) t<<"Notice: "<<notice()<<"\n";
    return out;
}

void GraphicsInfo::copyReport() const {
    QGuiApplication::clipboard()->setText(report());
}

int GraphicsInfo::printReport() {
    QTextStream out(stdout);
    GraphicsInfo info;
    // OpenGL: a context like Qt Quick's, without a window.
    QOffscreenSurface surface;
    surface.create();
    QOpenGLContext context;
    if(context.create() && context.makeCurrent(&surface)) {
        QOpenGLFunctions *f=context.functions();
        const auto text=[f](GLenum name){const GLubyte *s=f->glGetString(name);return s?QString::fromUtf8(reinterpret_cast<const char *>(s)):QString();};
        info.measured(context.isOpenGLES()?QStringLiteral("OpenGL ES"):QStringLiteral("OpenGL"),text(GL_VERSION),text(GL_VENDOR),text(GL_RENDERER),0,false);
        context.doneCurrent();
    } else out<<"OpenGL: no context could be created\n";
    out<<info.report();
    out<<"Qt Quick backend: "<<(applied().api==u"auto"?QStringLiteral("default (OpenGL on Linux)"):applied().api)<<"\n";
#if QT_CONFIG(vulkan)
    QVulkanInstance vulkan;
    vulkan.setApiVersion(QVersionNumber(1,0));
    if(vulkan.create()) {
        QVulkanFunctions *f=vulkan.functions();
        uint32_t count=0;
        f->vkEnumeratePhysicalDevices(vulkan.vkInstance(),&count,nullptr);
        QList<VkPhysicalDevice> devices(qsizetype(count),VK_NULL_HANDLE);
        f->vkEnumeratePhysicalDevices(vulkan.vkInstance(),&count,devices.data());
        out<<"Vulkan devices (Qt Quick takes #0 unless QT_VK_PHYSICAL_DEVICE_INDEX is set):\n";
        for(uint32_t i=0;i<count;++i) {
            VkPhysicalDeviceProperties p{};
            f->vkGetPhysicalDeviceProperties(devices.at(int(i)),&p);
            out<<"  #"<<i<<' '<<p.deviceName<<(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU?"  — SOFTWARE (CPU)":"")<<"\n";
        }
    } else if(vulkan.errorCode()!=VK_SUCCESS) out<<"Vulkan: not available (error "<<vulkan.errorCode()<<")\n";
    else out<<"Vulkan: not available on the "<<QGuiApplication::platformName()<<" platform\n";
#endif
    out.flush();
    return 0;
}
