#include "daemon_client.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>

#include "dbus_names.h"

DaemonClient::DaemonClient(QObject *parent)
    : QObject(parent)
    , m_watcher(QString(jarvis::dbus::kService), QDBusConnection::sessionBus(),
                QDBusServiceWatcher::WatchForRegistration
                    | QDBusServiceWatcher::WatchForUnregistration)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kReplyTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("timeout")); });

    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this,
            [this] { setAvailable(true); });
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        setAvailable(false);
        if (m_waiting)
            fail(QStringLiteral("daemon stopped"));
    });

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (QDBusConnectionInterface *iface = bus.interface()) {
        m_available = iface->isServiceRegistered(QString(jarvis::dbus::kService)).value();
    }

    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("Reply"), this,
                SLOT(onReplySignal(quint64, QString)));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("Progress"), this,
                SLOT(onProgressSignal(quint64, QString)));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("Routed"), this,
                SLOT(onRoutedSignal(quint64, QString)));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("Failed"), this,
                SLOT(onFailedSignal(quint64, QString)));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("Notice"), this,
                SLOT(onNoticeSignal(QString)));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("Suggestion"), this,
                SIGNAL(suggestion(QString)));
    bus.connect(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                QString(jarvis::dbus::kInterface), QStringLiteral("ContextChanged"), this,
                SIGNAL(contextChanged()));
}

void DaemonClient::cancel()
{
    if (!m_available || !m_waiting)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
                                                      QString(jarvis::dbus::kInterface), QStringLiteral("Cancel"));
    QDBusConnection::sessionBus().asyncCall(msg, kCallTimeoutMs);
}

void DaemonClient::abandon()
{
    m_timeout.stop();
    m_waiting = false;
    m_pendingId = 0;
}

void DaemonClient::onRoutedSignal(quint64 id, const QString &route)
{
    emit routed(route.left(20), m_waiting && id != 0 && id == m_pendingId);
}

void DaemonClient::onFailedSignal(quint64 id, const QString &reason)
{
    emit claudeFailed(reason.left(300), m_waiting && id != 0 && id == m_pendingId);
}

void DaemonClient::onNoticeSignal(const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.left(4096).toUtf8()).object();
    emit notice(o.value(QStringLiteral("kind")).toString().left(20), o.value(QStringLiteral("title")).toString().left(200));
}

void DaemonClient::tryStartDaemon()
{
    if (m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("StartServiceByName"));
    msg << QString(jarvis::dbus::kService) << 0u;
    QDBusConnection::sessionBus().asyncCall(msg, kCallTimeoutMs); // result is irrelevant
}

void DaemonClient::ask(const QString &text, const QStringList &files, const QString &desktop)
{
    if (m_waiting)
        return;

    m_waiting = true;
    m_pendingId = 0;
    m_timeout.start();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
        QString(jarvis::dbus::kInterface),
        !desktop.isEmpty() ? QStringLiteral("AskContext") : files.isEmpty() ? QStringLiteral("Ask") : QStringLiteral("AskFiles"));
    msg << text;
    if (!desktop.isEmpty())
        msg << files << desktop;
    else if (!files.isEmpty())
        msg << files;

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg, kCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                const QDBusPendingReply<quint64> reply = *w;
                w->deleteLater();
                if (!m_waiting)
                    return; // already failed or answered
                if (reply.isError()) {
                    fail(reply.error().message());
                } else if (reply.value() == 0) {
                    fail(QStringLiteral("request rejected by daemon"));
                } else {
                    m_pendingId = reply.value();
                }
            });
}

void DaemonClient::reloadConfig()
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
        QString(jarvis::dbus::kInterface), QStringLiteral("ReloadConfig"));
    QDBusConnection::sessionBus().asyncCall(msg, kCallTimeoutMs);
}

void DaemonClient::quitDaemon()
{
    if (!m_available)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(jarvis::dbus::kService), QString(jarvis::dbus::kPath),
        QString(jarvis::dbus::kInterface), QStringLiteral("Quit"));
    QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
}

void DaemonClient::onReplySignal(quint64 id, const QString &reply)
{
    // Reply is broadcast to every client; take only our own.
    if (!m_waiting || id == 0 || id != m_pendingId)
        return;
    m_timeout.stop();
    m_waiting = false;
    emit replyReady(reply);
}

void DaemonClient::onProgressSignal(quint64 id, const QString &text)
{
    if (!m_waiting || id == 0 || id != m_pendingId)
        return;
    m_timeout.start(); // still writing: keep waiting
    emit replyProgress(text);
}

void DaemonClient::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availabilityChanged(available);
}

void DaemonClient::fail(const QString &reason)
{
    m_timeout.stop();
    m_waiting = false;
    m_pendingId = 0;
    emit failed(reason);
}
