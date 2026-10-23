#include <QApplication>
#include <QSessionManager>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include "desktop_controller.h"
#include "notifier.h"
#include "build_config.h"
#include "config.h"
#include "language.h"
#include "visual_state.h"
#include "graphics_info.h"

// SIGTERM (logout, systemctl stop) ends the event loop normally, so a lesson
// recording is finished and saved instead of being cut off.
static int signalPipe[2]={-1,-1};
static void onTerminate(int) { const char c=1; [[maybe_unused]] auto n=::write(signalPipe[1],&c,1); }

// QML never fetches anything from the network: a remote image in rendered
// Markdown (an answer, a skill, lesson notes) would leak data in its URL.
// Defence in depth behind the Markdown sanitizer; local and qrc files still load.
class OfflineNetwork : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
protected:
    QNetworkReply *createRequest(Operation op,const QNetworkRequest &request,QIODevice *data) override {
        const QString scheme=request.url().scheme();
        if(scheme=="qrc" || scheme=="file" || scheme=="data") return QNetworkAccessManager::createRequest(op,request,data);
        return QNetworkAccessManager::createRequest(GetOperation,QNetworkRequest(QUrl(QStringLiteral("data:,"))),nullptr);
    }
};
class OfflineNetworkFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override { return new OfflineNetwork(parent); }
};

int main(int argc,char *argv[]) {
    // The graphics card (PRIME offload) must be chosen before Qt loads a driver.
    GraphicsInfo::prepareEnvironment(argc,argv);
    QApplication app(argc,argv);
    app.setApplicationName("jarvis");app.setOrganizationName("Jarvis");app.setApplicationDisplayName("J.A.R.V.I.S.");
    app.setApplicationVersion(JARVIS_VERSION);app.setDesktopFileName("org.jarvis.Jarvis");
    jarvis::applyUiLanguage(jarvis::Config::loadFileOnly().language);
    QCommandLineParser parser;parser.setApplicationDescription("Jarvis desktop assistant");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"quick","Open the quick command bar"});
    parser.addOption({"overlay","Open the Jarvis overlay above other windows (bind this to a key: Ctrl+J)"});
    parser.addOption({"tray","Start in the system tray without opening a window (autostart)"});
    parser.addOption({"open","Open a part of Jarvis: main, tasks, study, curious or memory","target"});
    parser.addOption({"gpu-info","Print which graphics API, renderer and GPU Jarvis draws with, and the GPUs found, then exit"});
    parser.addOption({"gpu","For this start: the graphics card for the interface (auto, integrated or dedicated)","card"});
    parser.addOption({"graphics-api","For this start: Qt's graphics API (auto, opengl or vulkan)","api"});
    parser.process(app);
    GraphicsInfo::applyApi();
    if(parser.isSet("gpu-info")) return GraphicsInfo::printReport();
    const QString open=parser.value("open").left(20);
    const bool quick=parser.isSet("quick");
    const bool overlay=parser.isSet("overlay");
    const bool tray=parser.isSet("tray");
    QDBusConnection bus=QDBusConnection::sessionBus();
    if(bus.isConnected() && !bus.registerService("org.jarvis.Desktop1")) {
        if(tray) return 0; // already running: autostart has nothing to do
        QDBusInterface existing("org.jarvis.Desktop1","/org/jarvis/Desktop1","org.jarvis.Desktop1",bus);
        // A shortcut daemon starts us with an activation token: hand it on, so the overlay may take the focus.
        auto reply=overlay?existing.call("ShowOverlay",qEnvironmentVariable("XDG_ACTIVATION_TOKEN"))
                  :!open.isEmpty()?existing.call("Open",open,QString()):existing.call(quick?"ShowQuick":"ShowMain");
        return reply.type()==QDBusMessage::ErrorMessage ? 1 : 0;
    }
    // This is the Jarvis that draws: a non-default graphics choice is now on trial.
    GraphicsInfo::armProbe();
    // Closing the window keeps Jarvis in the tray; "Quit" ends it.
    app.setQuitOnLastWindowClosed(false);
    // The session manager must not restore Jarvis: autostart starts it.
    QObject::connect(&app,&QGuiApplication::saveStateRequest,&app,[](QSessionManager &manager){manager.setRestartHint(QSessionManager::RestartNever);});
    if(::socketpair(AF_UNIX,SOCK_STREAM,0,signalPipe)==0) {
        auto *notifier=new QSocketNotifier(signalPipe[0],QSocketNotifier::Read,&app);
        QObject::connect(notifier,&QSocketNotifier::activated,&app,[]{char c;[[maybe_unused]] auto n=::read(signalPipe[0],&c,1);QCoreApplication::quit();});
        std::signal(SIGTERM,onTerminate);
        std::signal(SIGHUP,onTerminate);
    }
    DesktopController desktop;
    Notifier notifier;
    if(bus.isConnected())bus.registerObject("/org/jarvis/Desktop1",&desktop,QDBusConnection::ExportScriptableSlots);
    // The HUD's state machine lives in the core library (it is unit-tested there).
    qmlRegisterType<jarvis::VisualState>("Jarvis.Core",1,0,"VisualStateController");
    QQuickStyle::setStyle("Basic");
    OfflineNetworkFactory offline; // outlives the engine
    // Measures what really renders; only the compact overlay gets an alpha channel.
    GraphicsInfo graphics;
    QObject::connect(&graphics,&GraphicsInfo::restartRequested,&desktop,&DesktopController::restart);
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&offline);
    engine.rootContext()->setContextProperty("desktopBridge",&desktop);
    engine.rootContext()->setContextProperty("startQuick",quick);
    engine.rootContext()->setContextProperty("startOverlay",overlay);
    engine.rootContext()->setContextProperty("startTray",tray);
    engine.rootContext()->setContextProperty("startOpen",open);
    engine.rootContext()->setContextProperty("jarvisGraphics",&graphics);
    // JARVIS_HUD_STATE=<state> pins the HUD to one state (idle, listening, processing, speaking,
    // notification, warning, error) for visual checks; JARVIS_HUD_STATE=demo starts the demo.
    engine.rootContext()->setContextProperty("hudForcedState",qEnvironmentVariable("JARVIS_HUD_STATE"));
    engine.rootContext()->setContextProperty("notifier",&notifier);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreated,&app,[](QObject *object,const QUrl &){if(!object)QCoreApplication::exit(EXIT_FAILURE);},Qt::QueuedConnection);
    engine.load(QUrl("qrc:/qt/qml/Jarvis/Main.qml"));
    // Whichever window draws first tells what renders (all share the process's device).
    if(auto *root=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0))) {
        graphics.watch(root);
        for(QQuickWindow *window:root->findChildren<QQuickWindow*>()) graphics.watch(window);
    }
    // Development aid for checking the UI without a display:
    // JARVIS_UI_SNAPSHOT=out.png [JARVIS_UI_SNAPSHOT_SAY=text] saves the main window and exits.
    if(const QString shot=qEnvironmentVariable("JARVIS_UI_SNAPSHOT");!shot.isEmpty()) {
        QTimer::singleShot(1500,&app,[&engine,shot]{
            auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            if(!window){QCoreApplication::exit(2);return;}
            window->show();
            if(QObject *chat=window->findChild<QObject*>("chatModel"))
                QMetaObject::invokeMethod(chat,"say",Q_ARG(QString,qEnvironmentVariable("JARVIS_UI_SNAPSHOT_SAY")));
            // JARVIS_UI_SNAPSHOT_SEND=text sends a real request, to see the HUD follow it.
            if(QObject *chat=window->findChild<QObject*>("chatModel");chat&&qEnvironmentVariableIsSet("JARVIS_UI_SNAPSHOT_SEND"))
                QMetaObject::invokeMethod(chat,"send",Q_ARG(QString,qEnvironmentVariable("JARVIS_UI_SNAPSHOT_SEND")));
            // JARVIS_UI_SNAPSHOT_CENTER=<tab number> shows a tab of the control center instead.
            if(const QString tab=qEnvironmentVariable("JARVIS_UI_SNAPSHOT_CENTER");!tab.isEmpty())
                if(QObject *center=window->findChild<QObject*>("controlCenter")) {
                    if(QObject *tabs=center->findChild<QObject*>("controlTabs")) tabs->setProperty("currentIndex",tab.toInt());
                    QMetaObject::invokeMethod(center,"open");
                }
            // JARVIS_UI_SNAPSHOT_SKILL=<skill id>[:<tab>] shows a skill's page.
            if(const QString skill=qEnvironmentVariable("JARVIS_UI_SNAPSHOT_SKILL");!skill.isEmpty())
                if(QObject *center=window->findChild<QObject*>("controlCenter"))
                    QMetaObject::invokeMethod(center,"showSkill",Q_ARG(QVariant,skill.section(':',0,0)),Q_ARG(QVariant,skill.section(':',1)));
            // JARVIS_UI_SNAPSHOT_OVERLAY=compact|expanded opens the overlay and saves it instead.
            const QString overlayMode=qEnvironmentVariable("JARVIS_UI_SNAPSHOT_OVERLAY");
            if(!overlayMode.isEmpty()) {
                QMetaObject::invokeMethod(window,"showOverlay",Q_ARG(QVariant,QString()),Q_ARG(QVariant,QString()));
                if(auto *o=window->property("overlay").value<QObject*>()) o->setProperty("expanded",overlayMode==QLatin1String("expanded"));
            }
            // JARVIS_UI_SNAPSHOT_DELAY=<ms> waits longer, e.g. for the HUD demo to play some events.
            const int delay=qBound(200,qEnvironmentVariableIntValue("JARVIS_UI_SNAPSHOT_DELAY"),60000);
            QTimer::singleShot(qEnvironmentVariableIsSet("JARVIS_UI_SNAPSHOT_DELAY")?delay:1500,window,[window,shot]{
                // JARVIS_UI_SNAPSHOT_SCREEN=1 grabs the whole screen, with the compact overlay.
                QQuickWindow *target=window;
                if(qEnvironmentVariableIsSet("JARVIS_UI_SNAPSHOT_OVERLAY"))
                    if(auto *o=qobject_cast<QQuickWindow*>(window->property("overlay").value<QObject*>())) target=o;
                const QImage image=qEnvironmentVariableIsSet("JARVIS_UI_SNAPSHOT_SCREEN")?window->screen()->grabWindow(0).toImage():target->grabWindow();
                image.save(shot);QCoreApplication::quit();});
        });
    }
    return app.exec();
}
