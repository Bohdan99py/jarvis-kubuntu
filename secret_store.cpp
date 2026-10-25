#include "secret_store.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QMap>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

// org.freedesktop.Secret.Secret: (session, parameters, value, content type).
struct Secret
{
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const Secret &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, Secret &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

} // namespace

Q_DECLARE_METATYPE(Secret)

namespace jarvis::detail {

// Receives a prompt's Completed signal.
class PromptWaiter : public QObject
{
    Q_OBJECT
public:
    bool finished = false;
    bool dismissed = true;
public slots:
    void completed(bool wasDismissed, const QDBusVariant &)
    {
        finished = true;
        dismissed = wasDismissed;
        emit done();
    }
signals:
    void done();
};

} // namespace jarvis::detail

namespace jarvis {
namespace {

using StringMap = QMap<QString, QString>;
using detail::PromptWaiter;

const QString kService = u"org.freedesktop.secrets"_s;
const QString kPath = u"/org/freedesktop/secrets"_s;
const QString kServiceIface = u"org.freedesktop.Secret.Service"_s;
constexpr int kTimeoutMs = 4000;
constexpr int kPromptMs = 120000;

void registerTypes()
{
    static const bool done = [] {
        qDBusRegisterMetaType<Secret>();
        qDBusRegisterMetaType<StringMap>();
        return true;
    }();
    Q_UNUSED(done)
}


QDBusConnection bus()
{
    return QDBusConnection::sessionBus();
}

bool disabled()
{
    return qEnvironmentVariable("JARVIS_SECRET_SERVICE") == u"off";
}

QDBusMessage call(const QString &path, const QString &iface, const QString &method, const QVariantList &args, int timeout = kTimeoutMs)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, path, iface, method);
    msg.setArguments(args);
    return bus().call(msg, QDBus::Block, timeout);
}

StringMap attributes(const QString &account)
{
    return {{u"application"_s, u"org.jarvis.Jarvis"_s}, {u"account"_s, account}};
}

QList<QDBusObjectPath> paths(const QVariant &v)
{
    return qdbus_cast<QList<QDBusObjectPath>>(v);
}

bool isRoot(const QDBusObjectPath &p)
{
    return p.path().isEmpty() || p.path() == u"/";
}

// A "plain" session: the secret travels unencrypted over the user's own session
// bus, as libsecret does without its optional DH exchange.
QDBusObjectPath openSession(QString *error)
{
    const QDBusMessage reply = call(kPath, kServiceIface, u"OpenSession"_s,
                                    {u"plain"_s, QVariant::fromValue(QDBusVariant(QString()))});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store did not answer.");
        return {};
    }
    return reply.arguments().at(1).value<QDBusObjectPath>();
}

void closeSession(const QDBusObjectPath &session)
{
    if (!isRoot(session))
        call(session.path(), u"org.freedesktop.Secret.Session"_s, u"Close"_s, {});
}

// Shows the store's prompt (an unlock or a confirmation) and waits for it.
bool runPrompt(const QDBusObjectPath &prompt, QString *error)
{
    if (isRoot(prompt))
        return true;
    QEventLoop loop;
    PromptWaiter waiter;
    QObject::connect(&waiter, &PromptWaiter::done, &loop, &QEventLoop::quit);
    const bool connected = bus().connect(kService, prompt.path(), u"org.freedesktop.Secret.Prompt"_s, u"Completed"_s,
                                         &waiter, SLOT(completed(bool,QDBusVariant)));
    if (!connected) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store's prompt could not be shown.");
        return false;
    }
    call(prompt.path(), u"org.freedesktop.Secret.Prompt"_s, u"Prompt"_s, {QString()});
    QTimer::singleShot(kPromptMs, &loop, &QEventLoop::quit);
    loop.exec();
    bus().disconnect(kService, prompt.path(), u"org.freedesktop.Secret.Prompt"_s, u"Completed"_s, &waiter,
                     SLOT(completed(bool,QDBusVariant)));
    if (!waiter.finished || waiter.dismissed) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store was not unlocked.");
        return false;
    }
    return true;
}

// {unlocked, locked} items of the account.
bool search(const QString &account, QList<QDBusObjectPath> *unlocked, QList<QDBusObjectPath> *locked)
{
    const QDBusMessage reply = call(kPath, kServiceIface, u"SearchItems"_s, {QVariant::fromValue(attributes(account))});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
        return false;
    *unlocked = paths(reply.arguments().at(0));
    *locked = paths(reply.arguments().at(1));
    return true;
}

QDBusObjectPath defaultCollection()
{
    const QDBusMessage reply = call(kPath, kServiceIface, u"ReadAlias"_s, {u"default"_s});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().at(0).value<QDBusObjectPath>();
}

bool unlockPaths(const QList<QDBusObjectPath> &items, QString *error)
{
    if (items.isEmpty())
        return true;
    const QDBusMessage reply = call(kPath, kServiceIface, u"Unlock"_s, {QVariant::fromValue(items)});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store could not be unlocked.");
        return false;
    }
    return runPrompt(reply.arguments().at(1).value<QDBusObjectPath>(), error);
}

} // namespace

bool SecretStore::available()
{
    if (disabled() || !bus().isConnected())
        return false;
    QDBusConnectionInterface *iface = bus().interface();
    if (!iface)
        return false;
    if (!iface->isServiceRegistered(kService)) {
        const QDBusReply<QStringList> activatable = iface->activatableServiceNames();
        if (!activatable.isValid() || !activatable.value().contains(kService))
            return false;
    }
    registerTypes();
    return !isRoot(defaultCollection());
}

QString SecretStore::read(const QString &account, State *state)
{
    auto set = [state](State s) {
        if (state)
            *state = s;
    };
    if (!available()) {
        set(State::Unavailable);
        return {};
    }
    QList<QDBusObjectPath> unlocked, locked;
    if (!search(account, &unlocked, &locked)) {
        set(State::Error);
        return {};
    }
    if (unlocked.isEmpty()) {
        set(locked.isEmpty() ? State::Missing : State::Locked);
        return {};
    }
    const QDBusObjectPath session = openSession(nullptr);
    if (isRoot(session)) {
        set(State::Error);
        return {};
    }
    const QDBusMessage reply = call(unlocked.first().path(), u"org.freedesktop.Secret.Item"_s, u"GetSecret"_s,
                                    {QVariant::fromValue(session)});
    closeSession(session);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        set(State::Error);
        return {};
    }
    const Secret secret = qdbus_cast<Secret>(reply.arguments().at(0));
    const QString value = QString::fromUtf8(secret.value).trimmed();
    set(value.isEmpty() ? State::Missing : State::Found);
    return value;
}

bool SecretStore::write(const QString &account, const QString &label, const QString &secret, QString *error)
{
    if (!available()) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "There is no password store in this session.");
        return false;
    }
    const QDBusObjectPath collection = defaultCollection();
    QString why;
    const QDBusObjectPath session = openSession(&why);
    if (isRoot(session)) {
        if (error)
            *error = why;
        return false;
    }
    QVariantMap properties{{u"org.freedesktop.Secret.Item.Label"_s, label},
                           {u"org.freedesktop.Secret.Item.Attributes"_s, QVariant::fromValue(attributes(account))}};
    const Secret value{session, {}, secret.toUtf8(), u"text/plain"_s};
    auto create = [&] {
        return call(collection.path(), u"org.freedesktop.Secret.Collection"_s, u"CreateItem"_s,
                    {properties, QVariant::fromValue(value), true}, kTimeoutMs);
    };
    QDBusMessage reply = create();
    // A locked collection: unlock it (the store's prompt), then try once more.
    if (reply.type() == QDBusMessage::ErrorMessage && reply.errorName() == u"org.freedesktop.Secret.Error.IsLocked") {
        if (!unlockPaths({collection}, error)) {
            closeSession(session);
            return false;
        }
        reply = create();
    }
    bool ok = reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 2;
    if (ok) {
        const QDBusObjectPath item = reply.arguments().at(0).value<QDBusObjectPath>();
        const QDBusObjectPath prompt = reply.arguments().at(1).value<QDBusObjectPath>();
        if (isRoot(item))
            ok = runPrompt(prompt, &why);
    } else {
        why = QCoreApplication::translate("jarvis", "The password store refused to save the key.");
    }
    closeSession(session);
    if (!ok) {
        if (error)
            *error = why;
        return false;
    }
    // Only a key that reads back is saved.
    State state;
    if (read(account, &state) != secret.trimmed()) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The key was not found in the password store after saving.");
        return false;
    }
    return true;
}

bool SecretStore::remove(const QString &account, QString *error)
{
    if (!available())
        return true;
    QList<QDBusObjectPath> unlocked, locked;
    if (!search(account, &unlocked, &locked)) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store did not answer.");
        return false;
    }
    if (!locked.isEmpty() && !unlockPaths(locked, error))
        return false;
    for (const QDBusObjectPath &item : unlocked + locked) {
        const QDBusMessage reply = call(item.path(), u"org.freedesktop.Secret.Item"_s, u"Delete"_s, {});
        if (reply.type() != QDBusMessage::ReplyMessage) {
            if (error)
                *error = QCoreApplication::translate("jarvis", "The password store refused to delete the key.");
            return false;
        }
        if (!reply.arguments().isEmpty() && !runPrompt(reply.arguments().at(0).value<QDBusObjectPath>(), error))
            return false;
    }
    return true;
}

bool SecretStore::unlock(const QString &account, QString *error)
{
    if (!available()) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "There is no password store in this session.");
        return false;
    }
    QList<QDBusObjectPath> unlocked, locked;
    if (!search(account, &unlocked, &locked)) {
        if (error)
            *error = QCoreApplication::translate("jarvis", "The password store did not answer.");
        return false;
    }
    return unlockPaths(locked, error);
}

} // namespace jarvis

#include "secret_store.moc"
