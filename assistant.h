#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QString>
#include <QTimer>

#include "activity_store.h"
#include "awareness.h"
#include "chat_engine.h"
#include "claude_client.h"
#include "code_store.h"
#include "config.h"
#include "curiosity.h"
#include "driving_store.h"
#include "knowledge_store.h"
#include "language.h"
#include "learning_store.h"
#include "skill_store.h"
#include "task_store.h"
#include "reminder_parser.h"
#include "vocab_store.h"
#include "workspace.h"

#include <functional>

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

    // `files`: paths of attachments (images, PDF, code, schematics), up to 8.
    // `desktop`: the request comes with the user's desktop context (the Ctrl+J
    // overlay, "what's on my screen"): {screen: none|text|image, screenText}.
    // The screenshot itself, if any, is one of `files`.
    quint64 ask(const QString &input, const QStringList &files = {}, const QJsonObject &desktop = {});
    // Stops the answer being written (Claude's stream); the part so far stays.
    void cancel();
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
    // action: done, remove, snooze (the configured minutes), reopen. "" on success.
    QString taskAction(const QString &id, const QString &action);
    // Edits an item from the Tasks panel. `when`: a phrase like "tomorrow at 10"
    // ("" keeps the time, "-" makes it a task without a time); `repeat`: "keep" or
    // a repetition code (see ParsedReminder::repeat). Returns "" or an error.
    QString editTask(const QString &id, const QString &text, const QString &when, const QString &repeat);

    // From the desktop bridge (jarvisd only).
    void windowActivated(const QString &caption, const QString &appClass, const QString &desktopId,
                         const QString &handle = {}, bool fullscreen = false, const QString &output = {});
    // KWin reports focus while activity tracking or window awareness is on.
    bool wantsWindowReports() const;

    // ---- Cognitive awareness ----
    // What the user is doing now, as JSON for the overlay and the HUD
    // (Awareness::context plus pausedUntil and the suggestion summary).
    QString contextJson();
    // Text the GUI recognised on the focused window (screen permission only).
    void screenObserved(const QString &appKey, const QString &text);
    // Pause suggestions and background observation for `minutes` (0 resumes, -1 until tomorrow morning).
    QString pauseAwareness(int minutes);
    // The desktop's "Do not disturb" (org.freedesktop.Notifications Inhibited).
    void setDoNotDisturb(bool on) { m_dnd = on; }
    // What Jarvis is asked when the user accepts the suggestion behind notification `key`.
    QString assistPrompt(const QString &key) const { return m_assistPrompts.value(key); }
    void setScreenLocked(bool locked);
    void setTrackingStatus(const QString &status);

    // ---- Desktop (0.19.2) ----
    // jarvisd handles desktop commands from the chat and the voice ("prepare the
    // desktop for programming", "open the downloads folder"): the handler returns
    // the reply, or "" to let the request go on as usual. Without one (the window's
    // own engine) such requests are answered like any other.
    using DesktopHandler = std::function<QString(const workspace::Command &, Lang)>;
    void setDesktopHandler(DesktopHandler handler) { m_desktopHandler = std::move(handler); }
    void setWorkspace(const QString &profile, const QStringList &focus) { m_awareness.setWorkspace(profile, focus); }
    // The next open reminder or task: {id, text, due (ms, 0 none)}; empty when none.
    QJsonObject nextTask() const;

signals:
    void replyReady(quint64 id, const QString &reply);
    // The answer to `id` so far, while Claude writes it (hidden tags removed).
    void replyProgress(quint64 id, const QString &textSoFar);
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
    // Where request `id` is being answered, for the interface to show what Jarvis
    // is doing: recall (taught answers, facts, earlier conversations), learn, llm,
    // code, files, memory, tasks, calculator, schematic, system (live data), local.
    // Background work has id 0: reflect (memory review), plan (reading a time).
    void routed(quint64 id, const QString &route);
    // Claude could not answer `id` (network, key, quota…); the reply says why.
    void failed(quint64 id, const QString &reason);
    // An offer of help passed the importance filter: {category, title, body, score, evidence}.
    void suggested(const QJsonObject &suggestion);
    // The observed context changed noticeably (another application or activity); throttled.
    void contextChanged();

private:
    struct Request
    {
        quint64 id;
        QString text;
        QString mode;        // empty for chat, otherwise a code request
        QJsonObject context; // code requests only
        QStringList files;   // attachments of a chat request
        QJsonObject desktop; // desktop context that goes with it (see ask())
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
    // A question with attached files: straight to Claude (or a local summary of schematics).
    void answerWithFiles(const Request &r, Lang lang);
    // A question about the screen or the application the user is in.
    void answerWithDesktop(const Request &r, Lang lang);
    QString localDesktopReply(Lang lang) const;
    // The user's text plus the OCR text of the screen, when the request carries it.
    QString withScreenText(const QString &text) const;
    void suggestHelp(qint64 t);
    void learnFromAwareness(qint64 t);
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
    ReminderOptions reminderOptions() const;
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
    Awareness m_awareness;
    QJsonObject m_awarenessState;
    QJsonObject m_currentDesktop; // desktop context of the request being answered
    QHash<QString, QString> m_assistPrompts;
    QString m_lastContextKey;
    QTimer m_contextNotify;
    bool m_dnd = false;
    DrivingStore m_driving;
    bool m_cancelled = false;
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
    DesktopHandler m_desktopHandler;
    QHash<QString, QString> m_assistTargets; // notice key -> part of Jarvis to open
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
