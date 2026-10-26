#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <QSocketNotifier>

#include <csignal>
#include <memory>

#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "daemon_service.h"
#include "dbus_names.h"

namespace {

// Exit status when another jarvisd owns the bus name (RestartPreventExitStatus= in jarvis.service).
constexpr int kAlreadyRunning = 3;

// Classic self-pipe trick: the signal handler only write()s one byte,
// the Qt event loop reads it and quits cleanly (async-signal-safe).
int g_sigFd[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    const ssize_t r = ::write(g_sigFd[0], &byte, 1);
    (void)r;
}

bool installSignalHandlers()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_sigFd) != 0)
        return false;

    struct sigaction sa;
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    return sigaction(SIGINT, &sa, nullptr) == 0 && sigaction(SIGTERM, &sa, nullptr) == 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("jarvisd"));
    QCoreApplication::setOrganizationName(QStringLiteral("Jarvis"));

    std::unique_ptr<QSocketNotifier> sigNotifier;
    if (installSignalHandlers()) {
        sigNotifier = std::make_unique<QSocketNotifier>(g_sigFd[1], QSocketNotifier::Read);
        QObject::connect(sigNotifier.get(), &QSocketNotifier::activated, &app, [] {
            char byte;
            const ssize_t r = ::read(g_sigFd[1], &byte, 1);
            (void)r;
            qInfo() << "signal received, shutting down";
            QCoreApplication::quit();
        });
    } else {
        qWarning() << "could not install signal handlers; stop me with SIGKILL";
    }

    // A second daemon would only fail to own the name: say so with its own exit
    // status, which jarvis.service does not treat as a crash (no restart loop).
    if (QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
        bus && bus->isServiceRegistered(jarvis::dbus::daemonService()).value()) {
        qInfo().noquote() << "jarvisd: already running as" << jarvis::dbus::daemonService();
        return kAlreadyRunning;
    }

    // 0.21: keys into the password store, config_version 2 (idempotent). Under jarvis.service
    // ~/.config is read-only for the service: the lock fails at once and the window upgrades.
    if (const QString migrated = jarvis::Config::migrate(); !migrated.isEmpty())
        qInfo().noquote() << "settings:" << migrated;

    jarvis::DaemonService service;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &service, &jarvis::DaemonService::stopping);
    QString error;
    if (!service.start(&error)) {
        qCritical().noquote() << "jarvisd:" << error;
        return 1;
    }

    qInfo().noquote() << "jarvisd" << jarvis::DaemonService::version() << "ready on"
                      << jarvis::dbus::daemonService();
    return app.exec();
}
