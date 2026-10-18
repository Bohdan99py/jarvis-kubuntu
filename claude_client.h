#pragma once

#include <QJsonArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "language.h"

class QNetworkReply;

namespace jarvis {

// Minimal Claude Messages API client. Chat answers stream (SSE): long answers
// never hit a timeout and progress() shows them while they are written.
// Keeps a rolling conversation history. One request at a time; the caller serializes.
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
    // Off for clients whose system prompt sets the output language itself (translation).
    void setLanguageHint(bool on) { m_languageHint = on; }
    bool isConfigured() const noexcept { return !m_apiKey.isEmpty(); }
    QString model() const { return m_model; }

    // `context` is appended to the system prompt: skills, what Jarvis knows
    // about the user, relevant taught answers, optional activity. `attachments`
    // are content blocks (images, documents, files) sent before the text;
    // `attachmentNote` names them for the history once they are dropped from it.
    void ask(const QString &userText, Lang lang, bool forcedLanguage, const QString &context = {},
             const QJsonArray &attachments = {}, const QString &attachmentNote = {});

    // A one-off request outside the conversation (memory reflection): no
    // history is read or written. Result arrives in extracted().
    void extract(const QString &system, const QString &userText, int maxTokens);
    bool extracting() const noexcept { return m_extracting; }

signals:
    // On success `text` is the answer, otherwise a human-readable error.
    void finished(bool ok, const QString &text);
    // The answer so far, while it streams in (a few times per second at most).
    void progress(const QString &textSoFar);
    void extracted(bool ok, const QString &text);

private:
    struct Turn
    {
        QString role; // "user" or "assistant"
        QString text;
        QJsonArray blocks; // attachments of a user turn, kept for the latest few
        QString note;      // what was attached
    };
    // Files stay in the conversation for this many latest turns that have them.
    static constexpr int kTurnsWithFiles = 2;

    // Last 20 exchanges, and no more text than this (the oldest go first).
    static constexpr int kMaxHistoryMessages = 40;
    static constexpr int kMaxHistoryChars = 120000;
    // Room for a full listing or a long explanation; billed by what is written.
    static constexpr int kMaxTokens = 16000;
    // Without a byte for this long the request is given up (streaming keeps bytes coming).
    static constexpr int kTimeoutMs = 90000;

    struct Stream
    {
        QByteArray buffer;   // unparsed SSE bytes
        QByteArray raw;      // the body of a non-200 answer
        QString text;
        QString stopReason;
        QString error;
        qint64 lastProgress = 0;
        bool sawText = false;
    };
    void readStream(QNetworkReply *reply, Stream &stream);
    void handleReply(QNetworkReply *reply, Stream &stream, const QString &userText, const QJsonArray &blocks,
                     const QString &note, bool ru);

    QNetworkAccessManager m_net;
    QList<Turn> m_history;
    QString m_apiKey;
    QString m_model;
    QString m_system;
    int m_maxTokens = kMaxTokens;
    bool m_keepHistory = true;
    bool m_languageHint = true;
    bool m_extracting = false;
};

} // namespace jarvis
