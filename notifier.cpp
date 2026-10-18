#include "notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QStringList>
#include <QVariantMap>

namespace {
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
} // namespace

Notifier::Notifier(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Broadcast signals: every client hears them, only our ids matter.
    bus.connect(kService, kPath, kService, QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
    bus.connect(kService, kPath, kService, QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));
}

void Notifier::show(const QString &key, const QString &title, const QString &body, const QVariantList &actions,
                    int timeoutMs)
{
    QStringList flat;
    for (const QVariant &a : actions) {
        const QVariantList pair = a.toList();
        if (pair.size() == 2)
            flat << pair.at(0).toString() << pair.at(1).toString();
    }
    const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("org.jarvis.Jarvis")},
                            {QStringLiteral("urgency"), QVariant::fromValue<uchar>(1)}};
    auto msg = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("Notify"));
    // The body may be read as markup: text from memory or Claude stays plain.
    msg << QStringLiteral("Jarvis") << m_ids.value(key, 0u) << QStringLiteral("org.jarvis.Jarvis")
        << title.left(200) << body.left(1000).toHtmlEscaped() << flat << hints << timeoutMs;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<uint> reply = *w;
        w->deleteLater();
        if (reply.isError())
            return; // no notification server: nothing to show
        const uint id = reply.value();
        m_keys.remove(m_ids.value(key, 0u));
        m_ids.insert(key, id);
        m_keys.insert(id, key);
    });
}

void Notifier::showNotice(const QJsonObject &notice)
{
    QVariantList actions;
    for (const auto &a : notice.value(QStringLiteral("actions")).toArray())
        actions.append(QVariant(a.toArray().toVariantList()));
    // Reminders stay until the user reacts; hints go away by themselves.
    const bool sticky = notice.value(QStringLiteral("kind")).toString() == QLatin1String("reminder");
    show(notice.value(QStringLiteral("key")).toString(), notice.value(QStringLiteral("title")).toString(),
         notice.value(QStringLiteral("body")).toString(), actions, sticky ? 0 : -1);
}

void Notifier::close(const QString &key)
{
    const uint id = m_ids.value(key, 0u);
    if (id == 0)
        return;
    auto msg = QDBusMessage::createMethodCall(kService, kPath, kService, QStringLiteral("CloseNotification"));
    msg << id;
    QDBusConnection::sessionBus().asyncCall(msg, 3000);
}

void Notifier::onActionInvoked(uint id, const QString &action)
{
    const QString key = m_keys.value(id);
    if (!key.isEmpty())
        emit actionInvoked(key, action);
}

void Notifier::onClosed(uint id, uint)
{
    const QString key = m_keys.take(id);
    if (!key.isEmpty() && m_ids.value(key) == id)
        m_ids.remove(key);
}
