#include <QApplication>
#include <QSessionManager>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include "desktop_controller.h"
#include "notifier.h"
#include "build_config.h"
#include "config.h"
#include "language.h"

// SIGTERM (logout, systemctl stop) ends the event loop normally, so a lesson
// recording is finished and saved instead of being cut off.
static int signalPipe[2]={-1,-1};
static void onTerminate(int) { const char c=1; [[maybe_unused]] auto n=::write(signalPipe[1],&c,1); }

int main(int argc,char *argv[]) {
    QApplication app(argc,argv);
    app.setApplicationName("jarvis");app.setOrganizationName("Jarvis");app.setApplicationDisplayName("J.A.R.V.I.S.");
    app.setApplicationVersion(JARVIS_VERSION);app.setDesktopFileName("org.jarvis.Jarvis");
    jarvis::applyUiLanguage(jarvis::Config::loadFileOnly().language);
    QCommandLineParser parser;parser.setApplicationDescription("Jarvis desktop assistant");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"quick","Open the quick command bar"});
    parser.addOption({"tray","Start in the system tray without opening a window (autostart)"});
    parser.addOption({"open","Open a part of Jarvis: main, tasks, study, curious or memory","target"});parser.process(app);
    const QString open=parser.value("open").left(20);
    const bool quick=parser.isSet("quick");
    const bool tray=parser.isSet("tray");
    QDBusConnection bus=QDBusConnection::sessionBus();
    if(bus.isConnected() && !bus.registerService("org.jarvis.Desktop1")) {
        if(tray) return 0; // already running: autostart has nothing to do
        QDBusInterface existing("org.jarvis.Desktop1","/org/jarvis/Desktop1","org.jarvis.Desktop1",bus);
        auto reply=!open.isEmpty()?existing.call("Open",open,QString()):existing.call(quick?"ShowQuick":"ShowMain");
        return reply.type()==QDBusMessage::ErrorMessage ? 1 : 0;
    }
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
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("desktopBridge",&desktop);
    engine.rootContext()->setContextProperty("startQuick",quick);
    engine.rootContext()->setContextProperty("startTray",tray);
    engine.rootContext()->setContextProperty("startOpen",open);
    engine.rootContext()->setContextProperty("notifier",&notifier);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreated,&app,[](QObject *object,const QUrl &){if(!object)QCoreApplication::exit(EXIT_FAILURE);},Qt::QueuedConnection);
    engine.load(QUrl("qrc:/qt/qml/Jarvis/Main.qml"));
    return app.exec();
}
