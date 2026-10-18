#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "activity_store.h"
#include "chat_engine.h"
#include "claude_client.h"
#include "code_store.h"
#include "config.h"
#include "curiosity.h"
#include "knowledge_store.h"
#include "language.h"
#include "learning_store.h"
#include "skill_store.h"
#include "task_store.h"
#include "vocab_store.h"

namespace jarvis {

// The router: memory commands and live data locally, short small talk locally,
// everything else to Claude (when an API key is configured). Requests are
// served one at a time; each gets an id so several clients can share one
// assistant. Along the way it learns: facts and topics from the user's
// messages, corrections of its own answers, <memory> notes from Claude, the
// user's Jarvis actions and (opt-in) the focused desktop application.
class Assistant : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Assistant)

public:
    explicit Assistant(QObject *parent = nullptr);
    ~Assistant() override;

    quint64 ask(const QString &input);
    void reloadConfig();
    const ConfigData &config() const { return m_config; }
    QString teach(const QString &q, const QString &a);
    QString graph() const { return m_knowledge.graph(m_memory.items()); }
    bool claudeEnabled() const noexcept { return m_claude.isConfigured(); }

    // JSON for the memory panels: facts, topics, activity, settings.
    QString memoryJson();
    // "" on success, otherwise a message for the user.
    QString remember(const QString &text);
    // A fact id, "facts" (all facts, summaries and topics), "activity" (activity
    // log), "code", "lesson:<id>" or "episode:<id>".
    QString forget(const QString &what);
    void recordAction(const QString &id, const QString &label);
    // "Ask me something": the next curious question, marked as asked; "" if none.
    QString curiousQuestion();
    // 👍 / 👎 on the last chat answer. Returns Jarvis's reaction.
    QString feedback(bool good);

    // ---- Programming (VS Code extension) ----
    // mode: ask, explain, fix, tests, error. context: language, file, project,
    // selection, diagnostics[{line, severity, message}]. Reply via replyReady.
    quint64 askCode(const QString &mode, const QString &text, const QJsonObject &context);
    void codeActivity(const QString &language, const QString &project);
    void codeWorkspace(const QString &project, const QStringList &languages, const QStringList &frameworks);
    void codeDiagnostic(const QString &language, const QString &project, const QString &message);
    QString teachCode(const QString &language, const QString &problem, const QString &solution);
    QString rateCode(quint64 requestId, bool good);
    void setIdeClients(int count);

    // ---- Autonomy ----
    // Background work — reminders, hints, the daily memory review — runs only
    // where this assistant is the active brain: in jarvisd, or in a GUI that
    // has no daemon. Off by default.
    void setAutonomous(bool on);
    bool autonomous() const noexcept { return m_autonomous; }
    // The user chose `action` on the notification `key` (see notice()).
    // Returns what the GUI should open: "", "main", "tasks", "study", "curious" or "memory".
    QString noticeAction(const QString &key, const QString &action);
    // Tasks panel: a phrase like "tomorrow at 9 call mum" (the trigger word is
    // optional). Returns Jarvis's confirmation or what went wrong.
    QString addTask(const QString &text);
    // action: done, remove, snooze (10 minutes). "" on success.
    QString taskAction(const QString &id, const QString &action);

    // From the desktop bridge (jarvisd only).
    void windowActivated(const QString &caption, const QString &appClass, const QString &desktopId);
    void setScreenLocked(bool locked);
    void setTrackingStatus(const QString &status);

signals:
    void replyReady(quint64 id, const QString &reply);
    // Facts, examples or topics changed (graph and lists need a refresh).
    void memoryChanged();
    // Activity data changed; throttled.
    void activityChanged();
    void configReloaded();
    // A desktop notification to show:
    //   {key, kind, title, body, actions: [[id, label], …]}
    // kind: reminder, break, study, curious, morning, insight. The id "default"
    // is a click on the notification itself. Answers go to noticeAction().
    void notice(const QJsonObject &notice);

private:
    struct Request
    {
        quint64 id;
        QString text;
        QString mode;        // empty for chat, otherwise a code request
        QJsonObject context; // code requests only
    };
    struct CodeAnswer
    {
        QString language;
        QString problem;
        QString answer;
        QString lessonId;
    };

    static constexpr int kSmallTalkMaxWords = 4;

    void startNext();
    void answer(const Request &r, Lang lang);
    // Reminders and tasks in chat. False when `text` is not about them; true when
    // the request is answered (now, or later once Claude has read the time).
    bool handleTaskCommand(const Request &r, Lang lang);
    QString scheduleReminder(const QString &what, qint64 due, const QString &repeat, Lang lang);
    void absorbPlan(bool ok, const QString &text);
    void autonomyTick();
    void deliverDue(qint64 t);
    void proactiveHints(qint64 t);
    void morningSummary(qint64 t, Lang lang);
    void reviewMemory();
    void absorbReview(const QJsonObject &result);
    void emitNotice(const QString &key, const QString &kind, const QString &title, const QString &body,
                    const QList<std::pair<QString, QString>> &actions);
    Lang uiLang() const;
    void finish(quint64 id, const QString &reply, bool rememberable);
    void finishLater(quint64 id, const QString &reply, bool rememberable, int delayMs);
    Lang replyLanguage(const QString &text);
    bool handleMemoryCommand(const QString &text, Lang lang, QString *reply);
    QString buildContext(const QString &question) const;
    QString absorbClaudeReply(const QString &text);
    void notifyActivity();
    bool curious() const { return m_config.learnDialog && m_config.curiosity; }
    QJsonArray topTopics(int max) const;
    Curiosity::Question nextQuestion(const QJsonObject &state, Lang lang);
    void rememberTurn(const QString &question, const QString &answer);
    void reflect();
    void absorbReflection(bool ok, const QString &text);
    void learnFromActivity();
    void startCode(const Request &r);
    void finishCode(bool ok, const QString &text);
    void learnFromCode();
    QString learnedToday(Lang lang) const;

    ConfigData m_config;
    SkillStore m_skills;
    LearningStore m_memory;
    KnowledgeStore m_knowledge;
    VocabStore m_vocab; // read only: the study window owns the flashcards
    ActivityStore m_activity;
    ChatEngine m_engine;
    ClaudeClient m_claude;
    ClaudeClient m_coder;
    CodeStore m_code;
    TaskStore m_tasks;
    // Short structured jobs: reading the time of a reminder, the memory review.
    ClaudeClient m_planner;
    enum class PlanJob { None, Reminder, Review };
    PlanJob m_planJob = PlanJob::None;
    Request m_planRequest;
    Lang m_planLang = Lang::En;
    QTimer m_autonomyTick;
    bool m_autonomous = false;
    qint64 m_autonomySince = 0;
    qint64 m_lastChat = 0;
    // "напомни купить молоко" — "когда?" — "в 6 вечера": the text waits for its time.
    QString m_awaitingTime;
    qint64 m_awaitingSince = 0;
    QHash<quint64, CodeAnswer> m_codeAnswers;
    CodeAnswer m_pendingCode;
    QString m_pendingMode;
    int m_ideClients = 0;
    int m_codeBeats = 0;
    qint64 m_correctionAt = 0; // 👎 given: the next message is the right answer
    QQueue<Request> m_queue;
    QTimer m_activityTick;
    QTimer m_activityNotify;
    QTimer m_reflectTimer;
    QList<std::pair<QString, QString>> m_transcript;
    Curiosity::Question m_offered;
    qint64 m_lastReflection = 0;
    int m_newTurns = 0;
    QString m_trackingStatus;
    QString m_currentText;
    QString m_lastQuestion;
    QString m_lastAnswer;
    Lang m_sessionLang = Lang::En;
    quint64 m_current = 0;
    quint64 m_nextId = 0;
    int m_ticks = 0;
    bool m_busy = false;
};

} // namespace jarvis
