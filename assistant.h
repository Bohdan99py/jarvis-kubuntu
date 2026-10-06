#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "activity_store.h"
#include "chat_engine.h"
#include "claude_client.h"
#include "config.h"
#include "knowledge_store.h"
#include "language.h"
#include "learning_store.h"
#include "skill_store.h"

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
    // A fact id, "facts" (all facts and topics) or "activity" (activity log).
    QString forget(const QString &what);
    void recordAction(const QString &id, const QString &label);

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

private:
    struct Request
    {
        quint64 id;
        QString text;
    };

    static constexpr int kSmallTalkMaxWords = 4;

    void startNext();
    void finish(quint64 id, const QString &reply, bool rememberable);
    void finishLater(quint64 id, const QString &reply, bool rememberable, int delayMs);
    Lang replyLanguage(const QString &text);
    bool handleMemoryCommand(const QString &text, Lang lang, QString *reply);
    QString buildContext(const QString &question) const;
    QString absorbClaudeReply(const QString &text);
    void notifyActivity();

    ConfigData m_config;
    SkillStore m_skills;
    LearningStore m_memory;
    KnowledgeStore m_knowledge;
    ActivityStore m_activity;
    ChatEngine m_engine;
    ClaudeClient m_claude;
    QQueue<Request> m_queue;
    QTimer m_activityTick;
    QTimer m_activityNotify;
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
