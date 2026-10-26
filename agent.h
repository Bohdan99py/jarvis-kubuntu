#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

#include "agent_intents.h"
#include "agent_system.h"
#include "agent_tools.h"
#include "language.h"

namespace jarvis {

class ClaudeClient;
class LocalModel;

// The autonomous agent (0.22): the user says what they want; Jarvis works out
// the steps, checks that each is allowed, runs them against the system,
// reads back what really changed and reports it honestly.
//
//   text → rules (agent_intents) or a model's plan (local first, Claude only
//   when allowed) → validation and permissions → confirmation when needed →
//   execution in a worker thread (dependencies, time and step limits, stop) →
//   verified results → the reply.
//
// The task state — asked, planned, done, checked, waiting, failed — lives in
// one place (this class, saved to agent-task.json). It is the WorldState of
// the agent: there was none before 0.22. A restarted Jarvis marks an
// unfinished task interrupted and never resumes it by itself.
class Agent : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Agent)

public:
    struct Settings
    {
        QString mode = QStringLiteral("auto");   // auto, local
        bool cloudPlanning = false;              // auto mode: Claude may plan what the local model cannot
        QJsonObject permissions;                 // category -> allow, ask, off
        int maxSteps = 6;
        int maxSeconds = 60;
    };

    Agent(std::shared_ptr<agent::System> system, LocalModel *model, ClaudeClient *cloud, QObject *parent = nullptr);
    ~Agent() override;

    void configure(const Settings &s);
    const Settings &settings() const { return m_settings; }
    // Whether Claude may be asked at all right now (mode, key).
    std::function<bool()> cloudAvailable;
    // Diagnostics of parts outside the system (the voice): what → text.
    std::function<QString(const QString &what, Lang lang)> diagnoseExtra;

    // Takes a chat message if it is for the agent: then true, and the reply comes
    // through finished(id, …). False: the message is not about the laptop and goes
    // on to the usual chat. `chatFallback`: the local model may answer free
    // conversation too (no Claude for it).
    bool handle(quint64 id, const QString &text, Lang lang, bool chatFallback);
    // Free conversation through the local model; false when it is unavailable
    // (the reason is in unavailableReason()).
    bool converse(quint64 id, const QString &text, Lang lang);
    QString unavailableReason(Lang lang) const;
    bool localModelReady() const;

    bool busy() const { return m_running || m_planning; }
    // "Остановись": stops the task now — the step that runs is aborted where it can be,
    // no further step starts. Returns the reply (what already changed).
    void stop(quint64 id, Lang lang);
    // A message that should not wait in the queue (stop while a task runs).
    static bool isStop(const QString &text);

    // {mode, task: {...}, model: {...}, pending}
    QJsonObject status() const;
    // Capabilities of this computer for the diagnostics page.
    QJsonObject diagnostics(Lang lang);

    // A plan as a model gives it, through the same checks (the cloud planner, tests).
    void acceptPlanForTest(quint64 id, const QString &text, Lang lang, const QJsonObject &plan, const QString &source)
    {
        acceptPlan(id, text, lang, plan, source, {});
    }

    // The task file (agent-task.json) — tests point it elsewhere.
    static QString taskPath();

signals:
    void finished(quint64 id, const QString &reply, const QString &route);
    void progress(quint64 id, const QString &text);
    void changed();

private:
    struct Pending
    {
        QString kind; // confirm, choice
        QJsonArray steps;      // the plan waiting (confirm) or the step to repeat with a choice
        int stepIndex = -1;    // choice: which step asked
        QJsonArray choiceArgs;
        QStringList choices;
        QString source;
        qint64 at = 0;
        Lang lang = Lang::Ru;
    };

    void plan(quint64 id, const QString &text, Lang lang, const agent::Parsed &parsed, bool chat);
    void planWithModel(quint64 id, const QString &text, Lang lang, bool chat, const agent::Parsed &parsed);
    void planWithCloud(quint64 id, const QString &text, Lang lang, const agent::Parsed &parsed);
    void acceptPlan(quint64 id, const QString &text, Lang lang, const QJsonObject &answer, const QString &source,
                    const agent::Parsed &parsed);
    // Validation and permissions; then confirmation or execution.
    void start(quint64 id, const QString &text, Lang lang, QList<agent::Step> steps, const QString &source,
               const QStringList &notes = {}, bool confirmed = false);
    void execute(quint64 id, const QString &text, Lang lang, const QList<agent::Step> &steps, const QString &source,
                 const QStringList &notes, const QJsonObject &exclude = {});
    void taskDone(quint64 id, Lang lang, const QJsonArray &results, bool timedOut, const QStringList &notes);
    QString systemPrompt() const;
    QString stateData(Lang lang);
    QString permission(const QString &category) const;
    void saveTask();
    void loadTask();
    void reply(quint64 id, const QString &text, const QString &route);

    std::shared_ptr<agent::System> m_system;
    LocalModel *m_model;
    ClaudeClient *m_cloud;
    Settings m_settings;
    QJsonObject m_memory;     // preferred headphones, last device, volume step
    QJsonObject m_task;       // the current or last task
    QJsonObject m_undo;       // {steps, at, text}: what puts back the last task that changed something
    Pending m_pending;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QThread *m_worker = nullptr;
    QTimer m_deadline;
    quint64 m_taskRequest = 0;
    QString m_cloudSystem;
    bool m_running = false;
    bool m_planning = false;
    bool m_timedOut = false;
    quint64 m_planRequest = 0;
    Lang m_planLang = Lang::Ru;
    QString m_planText;
    agent::Parsed m_planParsed;
    QMetaObject::Connection m_cloudConnection;
};

} // namespace jarvis
