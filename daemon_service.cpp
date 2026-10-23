#include "daemon_service.h"

#include <QDBusConnection>
#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusVariant>
#include <QDBusMessage>
#include <QDebug>
#include <QDBusPendingCallWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "dbus_names.h"
#include "build_config.h"
#include "language.h"

namespace jarvis {

DaemonService::DaemonService(QObject *parent)
    : QObject(parent)
    , m_bridge(&m_assistant, dbus::daemonService())
    , m_ide(&m_assistant, IdeServer::defaultPath(dbus::daemonService()))
{
    new DaemonAdaptor(this); // child of this; exported with the object
    connect(&m_assistant, &Assistant::replyReady, this, &DaemonService::replyReady);
    connect(&m_assistant, &Assistant::notice, this, [this](const QJsonObject &notice) {
        m_noticeBodies.insert(notice.value(QStringLiteral("key")).toString(), notice.value(QStringLiteral("body")).toString());
        m_notifier.showNotice(notice);
    });
    connect(&m_notifier, &Notifier::actionInvoked, this, [this](const QString &key, const QString &action) {
        const QString target = m_assistant.noticeAction(key, action);
        if (target.isEmpty())
            return;
        // An accepted offer of help opens the overlay with the question already asked.
        const QString payload = target == QLatin1String("curious") ? m_noticeBodies.value(key)
                                : target == QLatin1String("assist") ? m_assistant.assistPrompt(key)
                                                                    : QString();
        openGui(target, payload);
    });
    // Plasma's "Do not disturb": no offers of help while it is on (reminders still come).
    m_dndPoll.setInterval(60 * 1000);
    connect(&m_dndPoll, &QTimer::timeout, this, &DaemonService::pollDoNotDisturb);
    m_dndPoll.start();
    QTimer::singleShot(3000, this, &DaemonService::pollDoNotDisturb);

}

void DaemonService::openGui(const QString &target, const QString &payload, int attempt)
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.jarvis.Desktop1"), QStringLiteral("/org/jarvis/Desktop1"),
                                              QStringLiteral("org.jarvis.Desktop1"), QStringLiteral("Open"));
    msg << target << payload;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, target, payload, attempt] {
        w->deleteLater();
        if (!w->isError() || attempt >= 4)
            return;
        // The daemon's sandbox is no place for the GUI: systemd starts it in the session.
        if (attempt == 0) {
            auto start = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                        QStringLiteral("/org/freedesktop/systemd1"),
                                                        QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                        QStringLiteral("StartUnit"));
            start << QStringLiteral("jarvis-tray.service") << QStringLiteral("replace");
            QDBusConnection::sessionBus().asyncCall(start, 3000);
        }
        QTimer::singleShot(1500, this, [this, target, payload, attempt] { openGui(target, payload, attempt + 1); });
    });
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
    m_bridge.apply(m_assistant.wantsWindowReports());
    m_assistant.setAutonomous(true); // reminders and hints come from here, GUI or not
    QString ideError;
    if (!m_ide.start(&ideError))
        qWarning().noquote() << "IDE socket unavailable:" << ideError; // chat keeps working
    return true;
}

void DaemonService::pollDoNotDisturb()
{
    auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.Notifications") << QStringLiteral("Inhibited");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 2000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        // Servers without the property (older specs) never inhibit.
        m_assistant.setDoNotDisturb(reply.type() == QDBusMessage::ReplyMessage
                                    && reply.arguments().value(0).value<QDBusVariant>().variant().toBool());
    });
}

bool DaemonService::callerIsGui() const
{
    if (!calledFromDBus())
        return true;
    const auto owner = connection().interface()->serviceOwner(QStringLiteral("org.jarvis.Desktop1"));
    return owner.isValid() && owner.value() == message().service();
}

quint64 DaemonService::ask(const QString &text, const QStringList &files, const QJsonObject &desktop)
{
    if ((text.isEmpty() && files.isEmpty()) || text.size() > kMaxRequestChars || files.size() > 8) {
        qWarning().nospace() << "rejected request (" << text.size() << " chars, " << files.size() << " files)";
        return 0;
    }
    for (const QString &f : files)
        if (f.size() > 4096 || !f.startsWith(u'/'))
            return 0;
    const quint64 id = m_assistant.ask(text, files, desktop);
    // Log the size only, never the content or the file names.
    qInfo().nospace() << "request #" << id << " (" << text.size() << " chars, " << files.size() << " files)";
    return id;
}

void DaemonService::reloadConfig()
{
    qInfo() << "reloading config";
    const bool wasWatching = m_assistant.wantsWindowReports();
    m_assistant.reloadConfig();
    applyUiLanguage(m_assistant.config().language);
    if (wasWatching != m_assistant.wantsWindowReports())
        m_bridge.apply(m_assistant.wantsWindowReports());
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
    connect(&service->assistant(), &Assistant::replyProgress, this, &DaemonAdaptor::Progress);
    connect(&service->assistant(), &Assistant::memoryChanged, this, &DaemonAdaptor::MemoryChanged);
    connect(&service->assistant(), &Assistant::activityChanged, this, &DaemonAdaptor::ActivityChanged);
    connect(&service->assistant(), &Assistant::routed, this, &DaemonAdaptor::Routed);
    connect(&service->assistant(), &Assistant::contextChanged, this, &DaemonAdaptor::ContextChanged);
    // What a suggestion shows on the desktop anyway: category, title and text — never the evidence.
    connect(&service->assistant(), &Assistant::suggested, this, [this](const QJsonObject &s) {
        emit Suggestion(QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact)));
    });
    connect(&service->assistant(), &Assistant::failed, this, [this](quint64 id, const QString &reason) { emit Failed(id, reason.left(500)); });
    // Only what a notification already shows on the desktop: kind and title.
    connect(&service->assistant(), &Assistant::notice, this, [this](const QJsonObject &notice) {
        emit Notice(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("kind"), notice.value(QStringLiteral("kind"))},
                                                                {QStringLiteral("title"), notice.value(QStringLiteral("title"))}})
                                          .toJson(QJsonDocument::Compact)));
    });
}

quint64 DaemonAdaptor::Ask(const QString &text)
{
    return m_service->ask(text);
}

quint64 DaemonAdaptor::AskFiles(const QString &text, const QStringList &files)
{
    return m_service->ask(text, files);
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
    // Any process of the user could call this; only KWin knows the real focus.
    const DaemonService &ctx = *m_service;
    if (!ctx.calledFromDBus() || !ctx.bridge().isKWin(ctx.message().service())) {
        if (ctx.calledFromDBus())
            ctx.sendErrorReply(QDBusError::AccessDenied, QStringLiteral("window reports are accepted from KWin only"));
        return;
    }
    m_service->assistant().windowActivated(caption.left(1000), appClass.left(200), desktopId.left(200));
}

void DaemonAdaptor::WindowFocused(const QString &caption, const QString &appClass, const QString &desktopId,
                                  const QString &handle, bool fullscreen, const QString &output)
{
    const DaemonService &ctx = *m_service;
    if (!ctx.calledFromDBus() || !ctx.bridge().isKWin(ctx.message().service())) {
        if (ctx.calledFromDBus())
            ctx.sendErrorReply(QDBusError::AccessDenied, QStringLiteral("window reports are accepted from KWin only"));
        return;
    }
    m_service->assistant().windowActivated(caption.left(1000), appClass.left(200), desktopId.left(200), handle.left(80),
                                           fullscreen, output.left(80));
}

QString DaemonAdaptor::Context()
{
    return m_service->assistant().contextJson();
}

quint64 DaemonAdaptor::AskContext(const QString &text, const QStringList &files, const QString &desktop)
{
    // The window title and screen text go to Claude with this request: only the Jarvis window may ask so.
    if (!m_service->callerIsGui() || desktop.size() > 16000) {
        m_service->sendErrorReply(QDBusError::AccessDenied, QStringLiteral("desktop context requests come from the Jarvis window only"));
        return 0;
    }
    const QJsonObject o = QJsonDocument::fromJson(desktop.toUtf8()).object();
    QJsonObject clean{{QStringLiteral("screen"), o.value(QStringLiteral("screen")).toString().left(10)}};
    if (const QString text = o.value(QStringLiteral("screenText")).toString(); !text.isEmpty())
        clean[QStringLiteral("screenText")] = text.left(8000);
    return m_service->ask(text, files, clean);
}

void DaemonAdaptor::ScreenObserved(const QString &appKey, const QString &text)
{
    if (!m_service->callerIsGui()) {
        m_service->sendErrorReply(QDBusError::AccessDenied, QStringLiteral("screen observations come from the Jarvis window only"));
        return;
    }
    m_service->assistant().screenObserved(appKey.left(200), text.left(8000));
}

QString DaemonAdaptor::PauseAwareness(int minutes)
{
    return m_service->assistant().pauseAwareness(minutes);
}

void DaemonAdaptor::Cancel()
{
    m_service->assistant().cancel();
}

QString DaemonAdaptor::Curious()
{
    return m_service->assistant().curiousQuestion();
}

QString DaemonAdaptor::Feedback(bool good)
{
    return m_service->assistant().feedback(good);
}

QString DaemonAdaptor::AddTask(const QString &text)
{
    if (text.size() > 1000)
        return QStringLiteral("too long");
    return m_service->assistant().addTask(text);
}

QString DaemonAdaptor::TaskAction(const QString &id, const QString &action)
{
    if (id.size() > 100 || action.size() > 20)
        return QStringLiteral("invalid");
    return m_service->assistant().taskAction(id, action);
}

QString DaemonAdaptor::EditTask(const QString &id, const QString &text, const QString &when, const QString &repeat)
{
    if (id.size() > 100 || text.size() > 1000 || when.size() > 300 || repeat.size() > 40)
        return QStringLiteral("invalid");
    return m_service->assistant().editTask(id, text, when, repeat);
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
