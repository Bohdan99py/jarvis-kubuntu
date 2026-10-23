#include "global_shortcut.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const QString kPortal = u"org.freedesktop.portal.Desktop"_s;
const QString kPath = u"/org/freedesktop/portal/desktop"_s;
const QString kInterface = u"org.freedesktop.portal.GlobalShortcuts"_s;

// a(sa{sv}): the portal's list of shortcuts.
struct PortalShortcut
{
    QString id;
    QVariantMap properties;
};
using PortalShortcuts = QList<PortalShortcut>;

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s)
{
    arg.beginStructure();
    arg << s.id << s.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s)
{
    arg.beginStructure();
    arg >> s.id >> s.properties;
    arg.endStructure();
    return arg;
}

} // namespace

Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcuts)

GlobalShortcut::GlobalShortcut(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"jarvis-global-shortcut"_s))
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcuts>();
    m_status = tr("Looking for the desktop's shortcut service…");
    // After the window is up: the portal may show a dialog.
    QTimer::singleShot(1500, this, &GlobalShortcut::start);
}

void GlobalShortcut::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

void GlobalShortcut::start()
{
    if (!m_bus.isConnected()) {
        setStatus(tr("No session bus."));
        return;
    }
    // Who is asking: the portal ties the shortcut to this application id. Fails
    // harmlessly on portals older than 1.19, which work out the id themselves.
    auto reg = QDBusMessage::createMethodCall(kPortal, kPath, u"org.freedesktop.host.portal.Registry"_s, u"Register"_s);
    reg << u"org.jarvis.Jarvis"_s << QVariantMap{};
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(reg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        auto version = QDBusMessage::createMethodCall(kPortal, kPath, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        version << kInterface << u"version"_s;
        auto *v = new QDBusPendingCallWatcher(m_bus.asyncCall(version, 3000), this);
        connect(v, &QDBusPendingCallWatcher::finished, this, [this, v] {
            v->deleteLater();
            const QDBusMessage reply = v->reply();
            if (reply.type() != QDBusMessage::ReplyMessage) {
                setStatus(tr("This desktop has no global shortcut portal. Bind \"Jarvis — overlay\" (jarvis --overlay) "
                             "in the system shortcut settings instead."));
                return;
            }
            m_version = reply.arguments().value(0).value<QDBusVariant>().variant().toUInt();
            m_available = true;
            m_bus.connect(kPortal, kPath, kInterface, u"Activated"_s, this, SLOT(onActivated(QDBusMessage)));
            createSession();
        });
    });
}

void GlobalShortcut::request(const QString &method, QVariantList args, QVariantMap options, const Callback &done)
{
    const QString token = u"jarvis_gs"_s + QString::number(++m_tokens);
    options.insert(u"handle_token"_s, token);
    QString sender = m_bus.baseService().mid(1);
    sender.replace(u'.', u'_');
    const QString path = u"/org/freedesktop/portal/desktop/request/"_s + sender + u'/' + token;
    m_pending.insert(path, done);
    // Subscribe before the call: the answer may come at once.
    m_bus.connect(kPortal, path, u"org.freedesktop.portal.Request"_s, u"Response"_s, this, SLOT(onResponse(QDBusMessage)));
    auto msg = QDBusMessage::createMethodCall(kPortal, kPath, kInterface, method);
    args.append(options);
    msg.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(msg, 10000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, path] {
        w->deleteLater();
        if (!w->isError())
            return;
        if (auto cb = m_pending.take(path))
            cb(2, {{u"error"_s, w->error().message()}});
    });
}

void GlobalShortcut::onResponse(const QDBusMessage &message)
{
    const QString path = message.path();
    m_bus.disconnect(kPortal, path, u"org.freedesktop.portal.Request"_s, u"Response"_s, this, SLOT(onResponse(QDBusMessage)));
    const Callback cb = m_pending.take(path);
    if (cb)
        cb(message.arguments().value(0).toUInt(), qdbus_cast<QVariantMap>(message.arguments().value(1)));
}

void GlobalShortcut::createSession()
{
    request(u"CreateSession"_s, {}, {{u"session_handle_token"_s, u"jarvis"_s}}, [this](uint response, const QVariantMap &results) {
        if (response != 0) {
            setStatus(tr("The desktop did not open a shortcut session: %1").arg(results.value(u"error"_s).toString()));
            return;
        }
        const QVariant handle = results.value(u"session_handle"_s);
        m_session = handle.canConvert<QDBusObjectPath>() ? handle.value<QDBusObjectPath>().path() : handle.toString();
        listShortcuts();
    });
}

void GlobalShortcut::takeShortcuts(const QVariantMap &results)
{
    PortalShortcuts list;
    const QVariant v = results.value(u"shortcuts"_s);
    if (v.canConvert<QDBusArgument>())
        v.value<QDBusArgument>() >> list;
    m_trigger.clear();
    for (const PortalShortcut &s : std::as_const(list))
        if (s.id == QLatin1String(kId))
            m_trigger = s.properties.value(u"trigger_description"_s).toString();
    if (m_trigger.isEmpty() && std::any_of(list.cbegin(), list.cend(), [](const PortalShortcut &s) { return s.id == QLatin1String(kId); }))
        m_trigger = tr("set, no key assigned");
}

void GlobalShortcut::listShortcuts()
{
    request(u"ListShortcuts"_s, {QVariant::fromValue(QDBusObjectPath(m_session))}, {}, [this](uint response, const QVariantMap &results) {
        if (response == 0)
            takeShortcuts(results);
        if (bound()) {
            setStatus(tr("Global shortcut: %1").arg(m_trigger));
            return;
        }
        // The first time only: the desktop asks the user to confirm Ctrl+J.
        QSettings settings;
        if (!settings.value(u"shortcut/asked"_s, false).toBool()) {
            settings.setValue(u"shortcut/asked"_s, true);
            bind();
        } else {
            setStatus(tr("No global shortcut yet. Press \"Bind Ctrl+J\" to ask the desktop for one."));
        }
    });
}

void GlobalShortcut::bind()
{
    if (!m_available || m_session.isEmpty() || m_binding) {
        if (!m_available)
            start();
        return;
    }
    m_binding = true;
    setStatus(tr("Waiting for you to confirm the shortcut in the desktop's dialog…"));
    const PortalShortcuts shortcuts{{QLatin1String(kId), {{u"description"_s, tr("Open the Jarvis overlay")},
                                                          {u"preferred_trigger"_s, QLatin1String(kPreferred)}}}};
    request(u"BindShortcuts"_s, {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(shortcuts), QString()}, {},
            [this](uint response, const QVariantMap &results) {
                m_binding = false;
                if (response == 0)
                    takeShortcuts(results);
                setStatus(bound() ? tr("Global shortcut: %1").arg(m_trigger)
                          : response == 1 ? tr("The shortcut was not confirmed. Press \"Bind Ctrl+J\" to try again.")
                                          : tr("The desktop could not bind the shortcut: %1").arg(results.value(u"error"_s).toString()));
            });
}

void GlobalShortcut::configure()
{
    if (configurable()) {
        auto msg = QDBusMessage::createMethodCall(kPortal, kPath, kInterface, u"ConfigureShortcuts"_s);
        msg << QVariant::fromValue(QDBusObjectPath(m_session)) << QString() << QVariantMap{};
        m_bus.asyncCall(msg, 5000);
        // The key may change there: read it again a little later.
        QTimer::singleShot(15000, this, &GlobalShortcut::listShortcuts);
        return;
    }
    // Plasma's shortcut settings, where application shortcuts can be changed by hand.
    if (!QProcess::startDetached(u"systemsettings"_s, {u"kcm_keys"_s}))
        QProcess::startDetached(u"kcmshell6"_s, {u"kcm_keys"_s});
}

void GlobalShortcut::onActivated(const QDBusMessage &message)
{
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2 || args.value(1).toString() != QLatin1String(kId))
        return;
    const QString session = args.value(0).value<QDBusObjectPath>().path();
    if (!m_session.isEmpty() && !session.isEmpty() && session != m_session)
        return;
    const QVariantMap options = qdbus_cast<QVariantMap>(args.value(3));
    qInfo("global shortcut pressed");
    emit activated(options.value(u"activation_token"_s).toString());
}
