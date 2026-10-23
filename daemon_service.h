#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include "activity_bridge.h"
#include "assistant.h"
#include "ide_server.h"
#include "integration_service.h"
#include "notifier.h"

namespace jarvis {

// The daemon's brain: owns the assistant and publishes it on the session bus.
// QDBusContext: Qt delivers the caller's message to the adaptor's parent.
class DaemonService : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DaemonService)

public:
    static constexpr int kMaxRequestChars = 4096;

    explicit DaemonService(QObject *parent = nullptr);
    ~DaemonService() override = default;

    // Registers object + well-known name on the session bus.
    bool start(QString *error);

    // Returns the request id, or 0 if the request was rejected.
    quint64 ask(const QString &text, const QStringList &files = {}, const QJsonObject &desktop = {});
    void reloadConfig();

    QString teach(const QString &q, const QString &a) { return m_assistant.teach(q, a); }
    QString graph() const { return m_assistant.graph(); }
    Assistant &assistant() { return m_assistant; }
    const ActivityBridge &bridge() const { return m_bridge; }
    static QString version();
    // True when the D-Bus caller is the Jarvis window (owner of org.jarvis.Desktop1).
    bool callerIsGui() const;
    // Shows a tab of the GUI; starts the tray application when it is not running.
    void openGui(const QString &target, const QString &payload, int attempt = 0);
    // The event loop is about to end (SIGTERM, Quit, background work off).
    void stopping() { m_integration.stopping(); }

signals:
    void replyReady(quint64 id, const QString &reply);

private:
    void pollDoNotDisturb();

    Assistant m_assistant;
    ActivityBridge m_bridge;
    IdeServer m_ide;
    Notifier m_notifier;
    IntegrationService m_integration;
    QHash<QString, QString> m_noticeBodies; // key -> text, for "answer the question"
    QTimer m_dndPoll;
};

// D-Bus face of DaemonService: interface org.jarvis.Daemon1
//   method Ask(s) -> t        queue a request, returns its id (0 = rejected)
//   method AskFiles(s, as) -> t  the same with attached files (paths, up to 8)
//   method Version() -> s
//   method Teach(s, s) -> s   save a question/answer example ("" = ok)
//   method Graph() -> s       memory graph JSON
//   method Memory() -> s      facts, topics, activity and learning settings JSON
//   method Remember(s) -> s   store a note about the user ("" = ok)
//   method Forget(s) -> s     fact id, "facts", "activity", "code", "lesson:<id>" or "episode:<id>" ("" = ok)
//   method RecordAction(s, s) a Jarvis action the user triggered (id, label)
//   method Curious() -> s     a question Jarvis wants to ask now ("" = none)
//   method Feedback(b) -> s   👍/👎 on the last chat answer; returns Jarvis's reaction
//   method AddTask(s) -> s    "tomorrow at 9 call mum" or a plain to-do; returns the confirmation
//   method TaskAction(s, s) -> s  task id and done|remove|snooze|reopen ("" = ok)
//   method EditTask(s, s, s, s) -> s  id, text, when ("" keeps, "-" no time), repeat ("keep" or a code)
//   method WindowActivated(s, s, s)  focused window; accepted only from KWin
//   method WindowFocused(s, s, s, s, b, s)  the same with KWin's window id, fullscreen and screen
//   method Context() -> s     what the user is doing (awareness context JSON)
//   method AskContext(s, as, s) -> t  a request with desktop context ({screen, screenText}); from the Jarvis window only
//   method ScreenObserved(s, s)  text recognised on the focused window (app key, text); from the Jarvis window only
//   method PauseAwareness(i) -> s  pause suggestions for N minutes (0 resumes, -1 until tomorrow)
//   method Cancel()           stop the answer being written
//   method ReloadConfig()     re-read ~/.config/jarvis/config.json
//   method Quit()             stop the daemon (clean exit, systemd will not restart it)
//   signal Reply(t, s)        answer for request id
//   signal Progress(t, s)     the answer so far, while Claude writes it
//   signal MemoryChanged()    facts/examples/topics changed
//   signal ActivityChanged()  activity data changed (throttled)
//   signal Routed(t, s)       how request id is being answered (llm, recall, calculator…; 0 = background)
//   signal Failed(t, s)       Claude could not answer request id, and why
//   signal Notice(s)          a notification Jarvis shows (JSON: key, kind, title, body)
//   signal Suggestion(s)      an offer of help passed the importance filter (JSON: category, title, body, score)
//   signal ContextChanged()   the observed context changed (another application or activity)
class DaemonAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jarvis.Daemon1")

public:
    explicit DaemonAdaptor(DaemonService *service);

public slots:
    quint64 Ask(const QString &text);
    quint64 AskFiles(const QString &text, const QStringList &files);
    QString Version();
    QString Teach(const QString &question, const QString &answer);
    QString Graph();
    QString Memory();
    QString Remember(const QString &text);
    QString Forget(const QString &what);
    void RecordAction(const QString &id, const QString &label);
    QString Curious();
    QString Feedback(bool good);
    QString AddTask(const QString &text);
    QString TaskAction(const QString &id, const QString &action);
    QString EditTask(const QString &id, const QString &text, const QString &when, const QString &repeat);
    void WindowActivated(const QString &caption, const QString &appClass, const QString &desktopId);
    void WindowFocused(const QString &caption, const QString &appClass, const QString &desktopId, const QString &handle,
                       bool fullscreen, const QString &output);
    QString Context();
    quint64 AskContext(const QString &text, const QStringList &files, const QString &desktop);
    void ScreenObserved(const QString &appKey, const QString &text);
    QString PauseAwareness(int minutes);
    void Cancel();
    void ReloadConfig();
    void Quit();

signals:
    void Reply(quint64 id, const QString &text);
    void Progress(quint64 id, const QString &text);
    void MemoryChanged();
    void ActivityChanged();
    void Routed(quint64 id, const QString &route);
    void Failed(quint64 id, const QString &reason);
    void Notice(const QString &json);
    void Suggestion(const QString &json);
    void ContextChanged();

private:
    DaemonService *m_service;
};

} // namespace jarvis
