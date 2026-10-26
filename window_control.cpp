#include "window_control.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include "dbus_names.h"
#include "runtime_paths.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QDBusMessage kwinCall(const QString &method, const QVariantList &args = {})
{
    auto msg = QDBusMessage::createMethodCall(u"org.kde.KWin"_s, u"/Scripting"_s, u"org.kde.kwin.Scripting"_s, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);
}

QString pluginName()
{
    const QString service = dbus::daemonService();
    return service == QLatin1String(dbus::kService) ? u"jarvis-windows"_s : u"jarvis-windows-"_s + service;
}

void unloadScript()
{
    if (kwinCall(u"isScriptLoaded"_s, {pluginName()}).arguments().value(0).toBool())
        kwinCall(u"unloadScript"_s, {pluginName()});
}

} // namespace

WindowControl::WindowControl(QObject *parent)
    : QObject(parent)
{
}

QString WindowControl::load(const QString &mode, const QString &id, const QString &token)
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(u"org.kde.KWin"_s).value())
        return u"KWin is not running"_s;
    QFile templ(jarvisDataFile(u"kwin/windows.js"_s));
    if (!templ.open(QIODevice::ReadOnly))
        return templ.fileName() + u": "_s + templ.errorString();
    QByteArray script = templ.readAll();
    // The data is JSON (a valid JavaScript literal); the service name is checked by dbus_names.
    script.replace("%DATA%", QJsonDocument(QJsonObject{{u"token"_s, token}, {u"mode"_s, mode}, {u"id"_s, id}}).toJson(QJsonDocument::Compact));
    script.replace("%SERVICE%", dbus::daemonService().toUtf8());
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    const QString path = dir + u'/' + pluginName() + u".js"_s;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(script) != script.size()
        || !out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || !out.commit())
        return path + u": "_s + out.errorString();
    unloadScript();
    const QDBusMessage loaded = kwinCall(u"loadScript"_s, {path, pluginName()});
    if (loaded.type() == QDBusMessage::ErrorMessage || loaded.arguments().value(0).toInt() < 0)
        return loaded.type() == QDBusMessage::ErrorMessage ? loaded.errorMessage() : u"KWin rejected the script"_s;
    kwinCall(u"start"_s);
    // Never left loaded: unloaded after the report, or after 5 s.
    QTimer::singleShot(5000, this, [] { unloadScript(); });
    return {};
}

QJsonObject WindowControl::run(const QString &mode, const QString &id, QString *error)
{
    QMutexLocker serial(&m_serial);
    const QString token = QString::number(QRandomGenerator::global()->generate64(), 36);
    {
        QMutexLocker lock(&m_mutex);
        m_token = token;
        m_answer = {};
    }
    QString failed;
    if (QThread::currentThread() == thread())
        failed = load(mode, id, token);
    else
        QMetaObject::invokeMethod(this, "load", Qt::BlockingQueuedConnection, Q_RETURN_ARG(QString, failed), Q_ARG(QString, mode),
                                  Q_ARG(QString, id), Q_ARG(QString, token));
    if (!failed.isEmpty()) {
        if (error)
            *error = failed;
        return {};
    }
    QMutexLocker lock(&m_mutex);
    if (m_answer.isEmpty())
        m_answered.wait(&m_mutex, 4000);
    m_token.clear();
    if (m_answer.isEmpty() && error)
        *error = u"KWin did not answer"_s;
    return m_answer;
}

bool WindowControl::report(const QJsonObject &o)
{
    if (!o.value(u"mode"_s).toString().startsWith(u"windows:"_s))
        return false;
    {
        QMutexLocker lock(&m_mutex);
        if (m_token.isEmpty() || o.value(u"token"_s).toString() != m_token)
            return true; // ours, but late or forged: ignored
        m_answer = o;
        m_answered.wakeAll();
    }
    QTimer::singleShot(0, this, [] { unloadScript(); });
    return true;
}

QList<agent::Window> WindowControl::windows(QString *error)
{
    QList<agent::Window> out;
    const QJsonObject o = run(u"list"_s, {}, error);
    for (const QJsonValue &v : o.value(u"windows"_s).toArray()) {
        const QJsonObject w = v.toObject();
        out.append({w.value(u"id"_s).toString(), w.value(u"app"_s).toString(), w.value(u"caption"_s).toString(), w.value(u"active"_s).toBool()});
    }
    return out;
}

QString WindowControl::act(const QString &mode, const QString &id)
{
    if (mode != u"activate" && mode != u"close")
        return u"unknown window action"_s;
    QString error;
    const QJsonObject o = run(mode, id, &error);
    if (!error.isEmpty())
        return error;
    return o.value(u"done"_s).toBool() ? QString() : u"the window is gone"_s;
}

} // namespace jarvis
