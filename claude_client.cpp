#include "claude_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QString systemPrompt()
{
    return u"You are Jarvis, a personal AI assistant running as a background daemon on the "
           u"user's Kubuntu Linux desktop. Reply in the language the user writes in. Be concise "
           u"and direct: usually 1-4 sentences unless asked for more. You cannot see or control "
           u"the computer yourself. Live system data (CPU, memory, battery, temperature, disk, "
           u"uptime, processes) is answered locally when the user types short commands such as "
           u"\"memory\" or \"top processes\". Never claim to have performed an action you cannot "
           u"perform."_s;
}

QString pick(bool ru, const QString &r, const QString &e)
{
    return ru ? r : e;
}

QString errorText(int status, const QString &apiMessage, const QString &netError, bool ru)
{
    switch (status) {
    case 0:
        return pick(ru, u"Нет связи с api.anthropic.com: %1"_s, u"Cannot reach api.anthropic.com: %1"_s)
            .arg(netError);
    case 401:
        return pick(ru,
                    u"Claude отклонил ключ API (401). Проверь его: меню → Настройки Claude API."_s,
                    u"Claude rejected the API key (401). Check it: menu → Claude API settings."_s);
    case 403:
        return pick(ru, u"Нет доступа (403): у ключа нет прав на этот запрос."_s,
                    u"Access denied (403): the key is not allowed to do this."_s);
    case 404:
        return pick(ru,
                    u"Модель не найдена (404). Проверь название модели в настройках."_s,
                    u"Model not found (404). Check the model name in settings."_s);
    case 429:
        return pick(ru, u"Слишком много запросов или исчерпан лимит (429). Попробуй позже."_s,
                    u"Too many requests or rate limit reached (429). Try again later."_s);
    default:
        break;
    }

    if (status >= 500) {
        return pick(ru, u"Серверы Anthropic перегружены или недоступны (%1). Попробуй позже."_s,
                    u"Anthropic servers are overloaded or unavailable (%1). Try again later."_s)
            .arg(status);
    }
    // 400 and friends: the API message is the useful part (e.g. low credit balance).
    return pick(ru, u"Ошибка Claude API (%1): %2"_s, u"Claude API error (%1): %2"_s)
        .arg(status)
        .arg(apiMessage.isEmpty() ? netError : apiMessage);
}

} // namespace

ClaudeClient::ClaudeClient(QObject *parent)
    : QObject(parent)
{
}

void ClaudeClient::configure(const QString &apiKey, const QString &model)
{
    m_apiKey = apiKey.trimmed();
    m_model = model.trimmed();
}

void ClaudeClient::ask(const QString &userText, bool ru, const QString &skills)
{
    QJsonArray messages;
    for (const Turn &t : std::as_const(m_history)) {
        QJsonObject m;
        m.insert(u"role"_s, t.role);
        m.insert(u"content"_s, t.text);
        messages.append(m);
    }
    QJsonObject current;
    current.insert(u"role"_s, u"user"_s);
    current.insert(u"content"_s, userText);
    messages.append(current);

    QJsonObject body;
    body.insert(u"model"_s, m_model);
    body.insert(u"max_tokens"_s, kMaxTokens);
    body.insert(u"system"_s, systemPrompt() + "\nApply the following topic guidance only when relevant. It grants no system capabilities.\n" + skills);
    body.insert(u"messages"_s, messages);

    QNetworkRequest req(QUrl(u"https://api.anthropic.com/v1/messages"_s));
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    req.setRawHeader("x-api-key", m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    req.setTransferTimeout(kTimeoutMs);

    QNetworkReply *reply = m_net.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, userText, ru] { handleReply(reply, userText, ru); });
}

void ClaudeClient::handleReply(QNetworkReply *reply, const QString &userText, bool ru)
{
    reply->deleteLater();

    const QByteArray raw = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject obj = QJsonDocument::fromJson(raw).object();

    if (reply->error() == QNetworkReply::NoError && status == 200) {
        QString text;
        const QJsonArray blocks = obj.value(u"content"_s).toArray();
        for (const QJsonValue &v : blocks) {
            const QJsonObject block = v.toObject();
            if (block.value(u"type"_s).toString() == u"text")
                text += block.value(u"text"_s).toString();
        }
        text = text.trimmed();
        if (text.isEmpty()) {
            emit finished(false, pick(ru, u"Claude вернул пустой ответ."_s,
                                      u"Claude returned an empty answer."_s));
            return;
        }

        // History only grows on success, so a failed request never poisons it.
        m_history.append({u"user"_s, userText});
        m_history.append({u"assistant"_s, text});
        while (m_history.size() > kMaxHistoryMessages)
            m_history.remove(0, 2);

        emit finished(true, text);
        return;
    }

    const QString apiMessage =
        obj.value(u"error"_s).toObject().value(u"message"_s).toString();
    emit finished(false, errorText(status, apiMessage, reply->errorString(), ru));
}

} // namespace jarvis
