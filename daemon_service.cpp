#include "daemon_service.h"

#include <QDBusConnection>
#include <QCoreApplication>
#include <QDBusError>
#include <QDebug>

#include "dbus_names.h"
#include "build_config.h"
#include "language.h"

namespace jarvis {

DaemonService::DaemonService(QObject *parent)
    : QObject(parent)
    , m_bridge(&m_assistant, dbus::daemonService())
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

    if (!bus.registerService(dbus::daemonService()))
        return fail(QStringLiteral("cannot own %1 (is jarvisd already running?): %2")
                        .arg(dbus::daemonService(), bus.lastError().message()));

    // The KWin script calls back into this name, so load it only once we own it.
    applyUiLanguage(m_assistant.config().language);
    m_bridge.apply(m_assistant.config().trackActivity);
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
    const bool wasTracking = m_assistant.config().trackActivity;
    m_assistant.reloadConfig();
    applyUiLanguage(m_assistant.config().language);
    if (wasTracking != m_assistant.config().trackActivity)
        m_bridge.apply(m_assistant.config().trackActivity);
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
    connect(&service->assistant(), &Assistant::memoryChanged, this, &DaemonAdaptor::MemoryChanged);
    connect(&service->assistant(), &Assistant::activityChanged, this, &DaemonAdaptor::ActivityChanged);
}

quint64 DaemonAdaptor::Ask(const QString &text)
{
    return m_service->ask(text);
}

QString DaemonAdaptor::Teach(const QString &q, const QString &a) { return m_service->teach(q, a); }
QString DaemonAdaptor::Graph() { return m_service->graph(); }
QString DaemonAdaptor::Memory() { return m_service->assistant().memoryJson(); }

QString DaemonAdaptor::Remember(const QString &text)
{
    if (text.size() > 4096)
        return QStringLiteral("too long");
    return m_service->assistant().remember(text);
}

QString DaemonAdaptor::Forget(const QString &what)
{
    if (what.isEmpty() || what.size() > 4096)
        return QStringLiteral("invalid");
    return m_service->assistant().forget(what);
}

void DaemonAdaptor::RecordAction(const QString &id, const QString &label)
{
    m_service->assistant().recordAction(id.left(200), label.left(200));
}

void DaemonAdaptor::WindowActivated(const QString &caption, const QString &appClass, const QString &desktopId)
{
    m_service->assistant().windowActivated(caption.left(1000), appClass.left(200), desktopId.left(200));
}

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
