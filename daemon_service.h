#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QObject>
#include <QString>

#include "activity_bridge.h"
#include "assistant.h"

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
    quint64 ask(const QString &text);
    void reloadConfig();

    QString teach(const QString &q, const QString &a) { return m_assistant.teach(q, a); }
    QString graph() const { return m_assistant.graph(); }
    Assistant &assistant() { return m_assistant; }
    const ActivityBridge &bridge() const { return m_bridge; }
    static QString version();

signals:
    void replyReady(quint64 id, const QString &reply);

private:
    Assistant m_assistant;
    ActivityBridge m_bridge;
};

// D-Bus face of DaemonService: interface org.jarvis.Daemon1
//   method Ask(s) -> t        queue a request, returns its id (0 = rejected)
//   method Version() -> s
//   method Teach(s, s) -> s   save a question/answer example ("" = ok)
//   method Graph() -> s       memory graph JSON
//   method Memory() -> s      facts, topics, activity and learning settings JSON
//   method Remember(s) -> s   store a note about the user ("" = ok)
//   method Forget(s) -> s     fact id, "facts" or "activity" ("" = ok)
//   method RecordAction(s, s) a Jarvis action the user triggered (id, label)
//   method Curious() -> s     a question Jarvis wants to ask now ("" = none)
//   method WindowActivated(s, s, s)  focused window; accepted only from KWin
//   method ReloadConfig()     re-read ~/.config/jarvis/config.json
//   method Quit()             stop the daemon (clean exit, systemd will not restart it)
//   signal Reply(t, s)        answer for request id
//   signal MemoryChanged()    facts/examples/topics changed
//   signal ActivityChanged()  activity data changed (throttled)
class DaemonAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jarvis.Daemon1")

public:
    explicit DaemonAdaptor(DaemonService *service);

public slots:
    quint64 Ask(const QString &text);
    QString Version();
    QString Teach(const QString &question, const QString &answer);
    QString Graph();
    QString Memory();
    QString Remember(const QString &text);
    QString Forget(const QString &what);
    void RecordAction(const QString &id, const QString &label);
    QString Curious();
    void WindowActivated(const QString &caption, const QString &appClass, const QString &desktopId);
    void ReloadConfig();
    void Quit();

signals:
    void Reply(quint64 id, const QString &text);
    void MemoryChanged();
    void ActivityChanged();

private:
    DaemonService *m_service;
};

} // namespace jarvis
