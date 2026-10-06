#include "daemon_service.h"

#include <QDBusConnection>
#include <QCoreApplication>
#include <QDBusError>
#include <QDebug>

#include "dbus_names.h"
#include "build_config.h"

namespace jarvis {

DaemonService::DaemonService(QObject *parent)
    : QObject(parent)
{
    new DaemonAdaptor(this); // child of this; exported with the object
    connect(&m_assistant, &Assistant::replyReady, this, &DaemonService::replyReady);
}

bool DaemonService::start(QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return fail(QStringLiteral("cannot connect to the session bus: %1")
                        .arg(bus.lastError().message()));

    // Object first, name second: nobody can find the name before the object exists.
    if (!bus.registerObject(dbus::kPath, this, QDBusConnection::ExportAdaptors))
        return fail(QStringLiteral("cannot register object %1: %2")
                        .arg(dbus::kPath, bus.lastError().message()));

    if (!bus.registerService(dbus::kService))
        return fail(QStringLiteral("cannot own %1 (is jarvisd already running?): %2")
                        .arg(dbus::kService, bus.lastError().message()));

    return true;
}

quint64 DaemonService::ask(const QString &text)
{
    if (text.isEmpty() || text.size() > kMaxRequestChars) {
        qWarning().nospace() << "rejected request (" << text.size() << " chars)";
        return 0;
    }
    const quint64 id = m_assistant.ask(text);
    // Log the size only, never the content.
    qInfo().nospace() << "request #" << id << " (" << text.size() << " chars)";
    return id;
}

void DaemonService::reloadConfig()
{
    qInfo() << "reloading config";
    m_assistant.reloadConfig();
}

QString DaemonService::version()
{
    return QStringLiteral(JARVIS_VERSION);
}

DaemonAdaptor::DaemonAdaptor(DaemonService *service)
    : QDBusAbstractAdaptor(service)
    , m_service(service)
{
    setAutoRelaySignals(false);
    connect(service, &DaemonService::replyReady, this, &DaemonAdaptor::Reply);
}

quint64 DaemonAdaptor::Ask(const QString &text)
{
    return m_service->ask(text);
}

QString DaemonAdaptor::Teach(const QString &q, const QString &a) { return m_service->teach(q, a); }
QString DaemonAdaptor::Graph() { return m_service->graph(); }

QString DaemonAdaptor::Version()
{
    return DaemonService::version();
}

void DaemonAdaptor::ReloadConfig()
{
    m_service->reloadConfig();
}

void DaemonAdaptor::Quit()
{
    qInfo() << "quit requested over D-Bus";
    // Queued: the D-Bus reply is sent first, then the event loop ends.
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit,
                              Qt::QueuedConnection);
}

} // namespace jarvis
