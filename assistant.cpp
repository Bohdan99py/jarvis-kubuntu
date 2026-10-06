#include "assistant.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>

#include "memory_text.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

QRegularExpression rx(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption
                                           | QRegularExpression::UseUnicodePropertiesOption
                                           | QRegularExpression::DotMatchesEverythingOption);
}

// " what am i doing " style matching on normalized text.
bool mentions(const QString &padded, const QStringList &phrases)
{
    for (const QString &p : phrases)
        if (padded.contains(u' ' + p + u' '))
            return true;
    return false;
}

QString quoteShort(const QString &s)
{
    const QString t = s.simplified();
    return t.size() > 80 ? t.left(77) + u"…"_s : t;
}

} // namespace

Assistant::Assistant(QObject *parent)
    : QObject(parent)
{
    connect(&m_claude, &ClaudeClient::finished, this, [this](bool ok, const QString &text) {
        finish(m_current, ok ? absorbClaudeReply(text) : text, ok);
    });
    m_activityTick.setInterval(60 * 1000);
    connect(&m_activityTick, &QTimer::timeout, this, [this] {
        m_activity.tick(now());
        if (++m_ticks % 5 == 0)
            m_activity.flush();
        notifyActivity();
    });
    m_activityNotify.setSingleShot(true);
    m_activityNotify.setInterval(1500);
    connect(&m_activityNotify, &QTimer::timeout, this, &Assistant::activityChanged);
    reloadConfig();
}

Assistant::~Assistant()
{
    m_activity.tick(now());
    m_activity.flush();
}

void Assistant::reloadConfig()
{
    m_config = Config::load();
    m_claude.configure(m_config.apiKey, m_config.model);
    if (m_claude.isConfigured())
        qInfo().noquote() << "Claude enabled, model" << m_config.model; // never log the key
    else
        qInfo() << "Claude disabled (no API key)";
    m_sessionLang = resolveLanguage(m_config.language);
    m_activity.setKeepTitles(m_config.trackTitles);
    if (!m_config.trackActivity) {
        m_activity.stop(now());
        m_activity.flush();
        m_activityTick.stop();
    }
    emit configReloaded();
}

QString Assistant::teach(const QString &q, const QString &a)
{
    const QString error = m_memory.teach(q, a);
    if (error.isEmpty())
        emit memoryChanged();
    return error;
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

Lang Assistant::replyLanguage(const QString &text)
{
    if (m_config.replyLanguage == u"ru")
        return Lang::Ru;
    if (m_config.replyLanguage == u"en")
        return Lang::En;
    Lang detected;
    if (detectLanguage(text, &detected))
        m_sessionLang = detected;
    return m_sessionLang;
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
    m_currentText = r.text;

    const Lang lang = replyLanguage(r.text);
    const bool ru = lang == Lang::Ru;

    QString commandReply;
    if (handleMemoryCommand(r.text, lang, &commandReply)) {
        finishLater(r.id, commandReply, false, 150);
        return;
    }

    QString localReply;
    const ChatEngine::Match match = m_engine.match(r.text, &localReply, lang);

    // Learning from the conversation itself: facts and topics of this message.
    // Live-data commands ("cpu", "память") say nothing about the user.
    if (m_config.learnDialog && match != ChatEngine::Match::Data) {
        m_knowledge.observe(r.text, true);
        emit memoryChanged();
    }
    if (match != ChatEngine::Match::Data) {
        QString learned = m_memory.recall(r.text);
        if (learned.isEmpty()) learned = m_skills.recall(r.text);
        if (!learned.isEmpty()) {
            finishLater(r.id, learned, true, 0);
            return;
        }
    }
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
        m_claude.ask(r.text, lang, m_config.replyLanguage != u"auto", buildContext(r.text));
        return;
    }

    if (match == ChatEngine::Match::None) {
        // Offline: a close taught example is better than "I can't".
        const auto similar = m_memory.similar(r.text, 0.6, 1);
        if (!similar.isEmpty()) {
            localReply = (ru ? u"Похоже на вопрос «%1», который вы мне объясняли:\n%2"_s
                             : u"This looks like \"%1\", which you taught me:\n%2"_s)
                             .arg(quoteShort(similar.first().question), similar.first().answer);
        } else {
            localReply += ru ? u"\n\nЧтобы я отвечал на любые вопросы, добавь ключ Claude API: "
                               u"меню → Настройки Claude API."_s
                             : u"\n\nTo let me answer anything, add a Claude API key: "
                               u"menu → Claude API settings."_s;
        }
    }

    // Short "thinking" pause so local answers feel like a conversation, not a flash.
    finishLater(r.id, localReply, match != ChatEngine::Match::Data,
                250 + int(QRandomGenerator::global()->bounded(350u)));
}

void Assistant::finishLater(quint64 id, const QString &reply, bool rememberable, int delayMs)
{
    QTimer::singleShot(delayMs, this, [this, id, reply, rememberable] { finish(id, reply, rememberable); });
}

void Assistant::finish(quint64 id, const QString &reply, bool rememberable)
{
    if (rememberable) {
        m_lastQuestion = m_currentText;
        m_lastAnswer = reply;
    }
    emit replyReady(id, reply);
    m_busy = false;
    startNext();
}

bool Assistant::handleMemoryCommand(const QString &input, Lang lang, QString *reply)
{
    const bool ru = lang == Lang::Ru;
    const QString textValue = input.trimmed();
    const QString padded = u' ' + text::normalize(textValue) + u' ';

    // "запомни ответ" — keep the previous question and answer as an example.
    static const QRegularExpression rememberAnswer = rx(
        u"^(?:(?:jarvis|джарвис)[,\\s]+)?(?:запомни(?:те)?\\s+(?:этот\\s+)?(?:ответ|это)|"
        u"remember\\s+(?:this|that)(?:\\s+answer)?|save\\s+(?:this|that)\\s+answer)\\s*[.!]?$"_s);
    if (rememberAnswer.match(textValue).hasMatch()) {
        if (m_lastQuestion.isEmpty()) {
            *reply = ru ? u"Пока нечего запоминать: сначала задайте вопрос."_s
                        : u"Nothing to remember yet: ask me something first."_s;
        } else {
            const QString error = teach(m_lastQuestion, m_lastAnswer);
            *reply = !error.isEmpty() ? error
                     : (ru ? u"Запомнил. На «%1» буду отвечать так же, даже без интернета."_s
                           : u"Saved. I'll answer \"%1\" the same way, even offline."_s)
                           .arg(quoteShort(m_lastQuestion));
        }
        return true;
    }

    // "нет, правильно: …" — a correction of the previous answer.
    static const QRegularExpression correction = rx(
        u"^(?:нет|неправильно|неверно|не так|ошибка|ошибаешься)[,.!:;\\s—-]+"
        u"(?:правильно|правильный ответ|верно|на самом деле|ответ)(?:\\s*(?:—|-|:|это|будет|такой))?\\s*:?\\s*(.{2,})$|"
        u"^(?:no|wrong|incorrect|nope)[,.!:;\\s-]+(?:the\\s+)?"
        u"(?:correct answer is|right answer is|answer is|actually|it's|it is|correct)\\s*:?\\s*(.{2,})$"_s);
    if (const auto m = correction.match(textValue); m.hasMatch() && !m_lastQuestion.isEmpty()) {
        const QString answer = (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).trimmed();
        const QString error = teach(m_lastQuestion, answer);
        if (error.isEmpty()) {
            m_lastAnswer = answer;
            *reply = (ru ? u"Понял, исправил. На «%1» теперь отвечаю: %2"_s
                         : u"Got it, corrected. For \"%1\" I'll now answer: %2"_s)
                         .arg(quoteShort(m_lastQuestion), answer);
        } else {
            *reply = error;
        }
        return true;
    }

    // "запомни, что …" / "remember that …"
    static const QRegularExpression rememberFact = rx(
        u"^(?:(?:jarvis|джарвис)[,\\s]+)?(?:запомни(?:те)?|учти|имей в виду|"
        u"remember|keep in mind)[,:\\s]+(?:что|that)?[,:\\s]*(.{3,})$"_s);
    const bool question = textValue.endsWith(u'?');
    if (const auto m = rememberFact.match(textValue); m.hasMatch() && !question) {
        const QString code = m_knowledge.remember(m.captured(1).trimmed(), u"manual"_s);
        if (code.isEmpty()) {
            emit memoryChanged();
            *reply = (ru ? u"Запомнил: %1"_s : u"Got it, I'll remember: %1"_s).arg(m.captured(1).trimmed());
        } else if (code == u"sensitive") {
            *reply = ru ? u"Это похоже на пароль, ключ или номер карты — такое я не сохраняю."_s
                        : u"That looks like a password, key or card number — I don't store those."_s;
        } else if (code == u"long") {
            *reply = ru ? u"Слишком длинно: заметка до 300 символов."_s
                        : u"Too long: notes are limited to 300 characters."_s;
        } else {
            *reply = ru ? u"Не удалось сохранить заметку."_s : u"Could not save the note."_s;
        }
        return true;
    }

    // "забудь …" / "forget …"
    static const QRegularExpression forgetRe = rx(
        u"^(?:(?:jarvis|джарвис)[,\\s]+)?(?:забудь(?:те)?|удали из памяти|forget)\\s+"
        u"(?:про|о|об|что|about|that)?\\s*(.{2,})$"_s);
    if (const auto m = forgetRe.match(textValue); m.hasMatch() && !question) {
        const QString what = m.captured(1).trimmed();
        static const QRegularExpression everything = rx(u"^(?:всё|все|все обо мне|всё обо мне|everything|everything about me|all)[.!]?$"_s);
        if (everything.match(what).hasMatch()) {
            *reply = ru ? u"Чтобы стереть всё, нажмите «Забыть всё» на вкладке «Обо мне» — так случайная фраза ничего не удалит."_s
                        : u"To erase everything, press \"Forget all\" in the \"About me\" tab, so a stray phrase can't wipe memory."_s;
            return true;
        }
        const int removed = m_knowledge.forget(what);
        if (removed > 0)
            emit memoryChanged();
        if (removed < 0)
            *reply = (ru ? u"Под это подходят %1 записей — уточните или удалите нужные на вкладке «Обо мне»."_s
                         : u"%1 entries match that — be more specific or remove them in the \"About me\" tab."_s)
                         .arg(-removed);
        else
            *reply = removed > 0 ? (ru ? u"Забыл (записей: %1)."_s : u"Forgotten (%1 entries)."_s).arg(removed)
                                 : (ru ? u"Не нашёл такого в памяти."_s : u"I have nothing like that in memory."_s);
        return true;
    }

    if (mentions(padded, {u"что ты обо мне знаешь"_s, u"что ты знаешь обо мне"_s, u"что ты помнишь обо мне"_s,
                          u"что ты обо мне помнишь"_s, u"что ты про меня знаешь"_s, u"что ты знаешь про меня"_s,
                          u"что ты запомнил"_s, u"what do you know about me"_s, u"what do you remember about me"_s,
                          u"what you know about me"_s, u"what have you learned about me"_s})) {
        *reply = m_knowledge.describe(lang);
        return true;
    }

    if (mentions(padded, {u"что я делаю"_s, u"что я сейчас делаю"_s, u"чем я занят"_s, u"чем я сейчас занят"_s,
                          u"над чем я работаю"_s, u"где я сейчас"_s, u"what am i doing"_s,
                          u"what am i working on"_s, u"what i am doing"_s})) {
        *reply = m_activity.currentText(lang, now(), m_config.trackActivity);
        return true;
    }

    if (mentions(padded, {u"чем я занимался"_s, u"что я делал сегодня"_s, u"моя активность"_s,
                          u"статистика активности"_s, u"сколько я сидел"_s, u"what did i do today"_s,
                          u"my activity"_s, u"activity today"_s, u"how did i spend"_s})) {
        *reply = m_activity.todayText(lang, now(), m_config.trackActivity);
        return true;
    }
    return false;
}

QString Assistant::buildContext(const QString &question) const
{
    QString ctx;
    const QString skills = m_skills.prompt();
    if (!skills.isEmpty())
        ctx += u"\n## Topic guidance\n"_s + skills;
    const QString profile = m_knowledge.profileForPrompt();
    if (!profile.isEmpty())
        ctx += u"\n## Known about the user\n"_s + profile;
    const auto similar = m_memory.similar(question, 0.34, 3);
    if (!similar.isEmpty()) {
        ctx += u"\n## Answers the user taught you (prefer them when they fit)\n"_s;
        for (const auto &m : similar)
            ctx += u"Q: "_s + m.question.left(300) + u"\nA: "_s + m.answer.left(600) + u'\n';
    }
    if (m_config.trackActivity && m_config.shareActivity) {
        const QString activity = m_activity.promptContext(now());
        if (!activity.isEmpty())
            ctx += u"\n## What the user is doing on the desktop\n"_s + activity;
    }
    return ctx;
}

QString Assistant::absorbClaudeReply(const QString &reply)
{
    static const QRegularExpression tag = rx(u"<memory>(.*?)</memory>"_s);
    QString shown = reply;
    int stored = 0;
    auto it = tag.globalMatch(reply);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString fact = m.captured(1).simplified();
        if (m_config.learnDialog && stored < 2 && !fact.isEmpty()
            && m_knowledge.remember(fact.left(KnowledgeStore::kMaxFactChars), u"claude"_s).isEmpty())
            ++stored;
    }
    shown.remove(tag);
    if (stored)
        emit memoryChanged();
    return shown.trimmed();
}

QString Assistant::memoryJson()
{
    const qint64 t = now();
    m_activity.reloadIfIdle();
    QJsonArray topics;
    const QJsonObject topicMap = m_knowledge.topics();
    QList<std::pair<int, QString>> order;
    for (auto it = topicMap.begin(); it != topicMap.end(); ++it)
        order.append({it.value().toInt(), it.key()});
    std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (int i = 0; i < order.size() && i < 30; ++i)
        topics.append(QJsonObject{{u"word"_s, order.at(i).second}, {u"count"_s, order.at(i).first}});

    const QJsonObject settings{
        {u"learnDialog"_s, m_config.learnDialog}, {u"trackActivity"_s, m_config.trackActivity},
        {u"trackTitles"_s, m_config.trackTitles}, {u"shareActivity"_s, m_config.shareActivity},
        {u"status"_s, m_trackingStatus}};
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {u"facts"_s, m_knowledge.facts()}, {u"topics"_s, topics},
        {u"examples"_s, m_memory.items().size()}, {u"activity"_s, m_activity.snapshot(t)},
        {u"settings"_s, settings}}).toJson(QJsonDocument::Compact));
}

QString Assistant::remember(const QString &textValue)
{
    const QString code = m_knowledge.remember(textValue, u"manual"_s);
    if (code.isEmpty()) {
        emit memoryChanged();
        return {};
    }
    if (code == u"sensitive")
        return QCoreApplication::translate("jarvis", "Passwords, keys and card numbers are never stored.");
    if (code == u"long")
        return QCoreApplication::translate("jarvis", "Notes are limited to 300 characters.");
    if (code == u"short")
        return QCoreApplication::translate("jarvis", "The note is too short.");
    return QCoreApplication::translate("jarvis", "Could not save the note.");
}

QString Assistant::forget(const QString &what)
{
    if (what == u"facts") {
        m_knowledge.clear();
        m_lastQuestion.clear();
        m_lastAnswer.clear();
        emit memoryChanged();
        return {};
    }
    if (what == u"activity") {
        m_activity.clear();
        notifyActivity();
        return {};
    }
    // From the UI: an id only, never a fuzzy text match.
    if (m_knowledge.forget(what, 0) <= 0)
        return QCoreApplication::translate("jarvis", "Nothing to forget: the entry was not found.");
    emit memoryChanged();
    return {};
}

void Assistant::recordAction(const QString &id, const QString &label)
{
    if (!m_config.learnDialog)
        return;
    m_activity.recordAction(id, label, now());
    if (!m_activityTick.isActive())
        m_activity.flush(); // not tracking: nothing else will save it
    notifyActivity();
}

void Assistant::windowActivated(const QString &caption, const QString &appClass, const QString &desktopId)
{
    if (!m_config.trackActivity)
        return;
    m_activity.windowActivated(caption, appClass, desktopId, now());
    if (!m_activityTick.isActive())
        m_activityTick.start();
    notifyActivity();
}

void Assistant::setScreenLocked(bool locked)
{
    m_activity.setLocked(locked, now());
    if (locked)
        m_activity.flush();
}

void Assistant::setTrackingStatus(const QString &status)
{
    if (m_trackingStatus == status)
        return;
    m_trackingStatus = status;
    notifyActivity();
}

void Assistant::notifyActivity()
{
    if (!m_activityNotify.isActive())
        m_activityNotify.start();
}

} // namespace jarvis
