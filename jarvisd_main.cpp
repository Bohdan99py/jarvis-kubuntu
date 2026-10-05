#include <QCoreApplication>
#include <QDebug>
#include <QSocketNotifier>

#include <csignal>
#include <memory>

#include <sys/socket.h>
#include <unistd.h>

#include "daemon_service.h"
#include "dbus_names.h"

namespace {

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

    jarvis::DaemonService service;
    QString error;
    if (!service.start(&error)) {
        qCritical().noquote() << "jarvisd:" << error;
        return 1;
    }

    qInfo().noquote() << "jarvisd" << jarvis::DaemonService::version() << "ready on"
                      << jarvis::dbus::kService;
    return app.exec();
}
