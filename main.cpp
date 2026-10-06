#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include "desktop_controller.h"
#include "build_config.h"
#include "config.h"
#include "language.h"

int main(int argc,char *argv[]) {
    QApplication app(argc,argv);
    app.setApplicationName("jarvis");app.setOrganizationName("Jarvis");app.setApplicationDisplayName("J.A.R.V.I.S.");
    app.setApplicationVersion(JARVIS_VERSION);app.setDesktopFileName("org.jarvis.Jarvis");
    jarvis::applyUiLanguage(jarvis::Config::loadFileOnly().language);
    QCommandLineParser parser;parser.setApplicationDescription("Jarvis desktop assistant");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"quick","Open the quick command bar"});parser.process(app);
    const bool quick=parser.isSet("quick");
    QDBusConnection bus=QDBusConnection::sessionBus();
    if(bus.isConnected() && !bus.registerService("org.jarvis.Desktop1")) {
        QDBusInterface existing("org.jarvis.Desktop1","/org/jarvis/Desktop1","org.jarvis.Desktop1",bus);
        auto reply=existing.call(quick?"ShowQuick":"ShowMain");
        return reply.type()==QDBusMessage::ErrorMessage ? 1 : 0;
    }
    DesktopController desktop;
    if(bus.isConnected())bus.registerObject("/org/jarvis/Desktop1",&desktop,QDBusConnection::ExportScriptableSlots);
    QQuickStyle::setStyle("Basic");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("desktopBridge",&desktop);
    engine.rootContext()->setContextProperty("startQuick",quick);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreated,&app,[](QObject *object,const QUrl &){if(!object)QCoreApplication::exit(EXIT_FAILURE);},Qt::QueuedConnection);
    engine.load(QUrl("qrc:/qt/qml/Jarvis/Main.qml"));
    return app.exec();
}
