#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "language.h"

class QNetworkReply;

namespace jarvis {

// Minimal Claude Messages API client (non-streaming). Keeps a short rolling
// conversation history. One request at a time; the caller serializes.
class ClaudeClient : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ClaudeClient)

public:
    explicit ClaudeClient(QObject *parent = nullptr);
    ~ClaudeClient() override = default;

    void configure(const QString &apiKey, const QString &model);
    // For specialised clients (code): replace the Jarvis persona, keep no
    // conversation history, allow longer answers.
    void setSystemPrompt(const QString &prompt) { m_system = prompt; }
    void setKeepHistory(bool keep) { m_keepHistory = keep; }
    void setMaxTokens(int tokens) { m_maxTokens = tokens; }
    bool isConfigured() const noexcept { return !m_apiKey.isEmpty(); }
    QString model() const { return m_model; }

    // `context` is appended to the system prompt: skills, what Jarvis knows
    // about the user, relevant taught answers, optional activity.
    void ask(const QString &userText, Lang lang, bool forcedLanguage, const QString &context = {});

    // A one-off request outside the conversation (memory reflection): no
    // history is read or written. Result arrives in extracted().
    void extract(const QString &system, const QString &userText, int maxTokens);
    bool extracting() const noexcept { return m_extracting; }

signals:
    // On success `text` is the answer, otherwise a human-readable error.
    void finished(bool ok, const QString &text);
    void extracted(bool ok, const QString &text);

private:
    struct Turn
    {
        QString role; // "user" or "assistant"
        QString text;
    };

    static constexpr int kMaxHistoryMessages = 20;
    static constexpr int kMaxTokens = 1024;
    static constexpr int kTimeoutMs = 60000;

    void handleReply(QNetworkReply *reply, const QString &userText, bool ru);

    QNetworkAccessManager m_net;
    QList<Turn> m_history;
    QString m_apiKey;
    QString m_model;
    QString m_system;
    int m_maxTokens = kMaxTokens;
    bool m_keepHistory = true;
    bool m_extracting = false;
};

} // namespace jarvis
