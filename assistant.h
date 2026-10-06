#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

#include "learning_store.h"
#include "skill_store.h"
#include "chat_engine.h"
#include "claude_client.h"

namespace jarvis {

// The router: local rules first for data and short small talk, Claude for
// everything else (when an API key is configured). Requests are served one
// at a time; each gets an id so several clients can share one assistant.
class Assistant : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Assistant)

public:
    explicit Assistant(QObject *parent = nullptr);
    ~Assistant() override = default;

    quint64 ask(const QString &input);
    void reloadConfig();
    QString teach(const QString &q, const QString &a) { return m_memory.teach(q, a); }
    QString graph() const { return m_memory.graph(); }
    bool claudeEnabled() const noexcept { return m_claude.isConfigured(); }

signals:
    void replyReady(quint64 id, const QString &reply);

private:
    struct Request
    {
        quint64 id;
        QString text;
    };

    static constexpr int kSmallTalkMaxWords = 4;

    void startNext();
    void finish(quint64 id, const QString &reply);

    SkillStore m_skills;
    LearningStore m_memory;
    ChatEngine m_engine;
    ClaudeClient m_claude;
    QQueue<Request> m_queue;
    quint64 m_current = 0;
    quint64 m_nextId = 0;
    bool m_busy = false;
};

} // namespace jarvis
