#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

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
    bool isConfigured() const noexcept { return !m_apiKey.isEmpty(); }
    QString model() const { return m_model; }

    void ask(const QString &userText, bool ru, const QString &skills = {});

signals:
    // On success `text` is the answer, otherwise a human-readable error.
    void finished(bool ok, const QString &text);

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
};

} // namespace jarvis
