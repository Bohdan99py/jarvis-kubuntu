#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("jarvis"));
    QGuiApplication::setOrganizationName(QStringLiteral("Jarvis"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("J.A.R.V.I.S."));
    // Must match the .desktop file name so KDE/Wayland maps the window to its icon and entry.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.jarvis.Jarvis"));

    // "Basic" is fully customizable and does not depend on KDE/Breeze styles being installed.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);

    engine.loadFromModule("Jarvis", "Main");

    return app.exec();
}
