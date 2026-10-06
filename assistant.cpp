#include "assistant.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QTimer>

#include "config.h"

using namespace Qt::StringLiterals;

namespace jarvis {

Assistant::Assistant(QObject *parent)
    : QObject(parent)
{
    connect(&m_claude, &ClaudeClient::finished, this,
            [this](bool, const QString &text) { finish(m_current, text); });
    reloadConfig();
}

void Assistant::reloadConfig()
{
    const ConfigData d = Config::load();
    m_claude.configure(d.apiKey, d.model);
    if (m_claude.isConfigured())
        qInfo().noquote() << "Claude enabled, model" << d.model; // never log the key
    else
        qInfo() << "Claude disabled (no API key)";
}

quint64 Assistant::ask(const QString &input)
{
    if (input.trimmed().isEmpty() || input.size() > 4096 || m_queue.size() >= 32) return 0;
    const quint64 id = ++m_nextId;
    m_queue.enqueue({id, input});
    if (!m_busy)
        startNext();
    return id;
}

void Assistant::startNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        return;
    }
    m_busy = true;
    const Request r = m_queue.dequeue();
    m_current = r.id;

    QString localReply;
    const ChatEngine::Match match = m_engine.match(r.text, &localReply);
    if (match != ChatEngine::Match::Data) {
        QString learned = m_memory.recall(r.text);
        if (learned.isEmpty()) learned = m_skills.recall(r.text);
        if (!learned.isEmpty()) {
            QTimer::singleShot(0, this, [this, r, learned] { finish(r.id, learned); });
            return;
        }
    }
    const bool ru = ChatEngine::looksRussian(r.text);
    const bool claude = m_claude.isConfigured();

    bool useLocal = false;
    switch (match) {
    case ChatEngine::Match::Data:
        useLocal = true; // live data is never sent anywhere
        break;
    case ChatEngine::Match::SmallTalk:
        // "привет" stays local; a long sentence that merely contains "как ты" goes to Claude.
        useLocal = !claude || r.text.simplified().split(QChar(u' '), Qt::SkipEmptyParts).size()
                                  <= kSmallTalkMaxWords;
        break;
    case ChatEngine::Match::None:
        useLocal = !claude;
        break;
    }

    if (!useLocal) {
        m_claude.ask(r.text, ru, m_skills.prompt());
        return;
    }

    if (match == ChatEngine::Match::None) {
        localReply += ru ? u"\n\nЧтобы я отвечал на любые вопросы, добавь ключ Claude API: "
                           u"меню → Настройки Claude API."_s
                         : u"\n\nTo let me answer anything, add a Claude API key: "
                           u"menu → Claude API settings."_s;
    }

    // Short "thinking" pause so local answers feel like a conversation, not a flash.
    const int delayMs = 250 + int(QRandomGenerator::global()->bounded(350u));
    const quint64 id = r.id;
    QTimer::singleShot(delayMs, this, [this, id, localReply] { finish(id, localReply); });
}

void Assistant::finish(quint64 id, const QString &reply)
{
    emit replyReady(id, reply);
    m_busy = false;
    startNext();
}

} // namespace jarvis
