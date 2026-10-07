#include "curiosity.h"

#include <QRegularExpression>
#include <QSet>

#include "memory_text.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

bool hasSlot(const QJsonArray &facts, const QString &slot)
{
    for (const auto &v : facts)
        if (v.toObject().value(u"slot"_s).toString() == slot)
            return true;
    return false;
}

// Some fact already mentions this app or word.
bool mentioned(const QJsonArray &facts, const QString &word)
{
    const QString needle = text::normalize(word);
    if (needle.isEmpty())
        return true;
    for (const auto &v : facts)
        if ((u' ' + text::normalize(v.toObject().value(u"value"_s).toString()) + u' ').contains(u' ' + needle + u' '))
            return true;
    return false;
}

bool recentlyAsked(const QJsonObject &state, const QString &id, qint64 nowMs)
{
    const double at = state.value(u"asked"_s).toObject().value(id).toDouble(-1);
    return at >= 0 && nowMs - qint64(at) < Curiosity::kRepeatAfterMs;
}

QString tr2(Lang lang, const QString &ru, const QString &en)
{
    return lang == Lang::Ru ? ru : en;
}

} // namespace

void Curiosity::countMessage(QJsonObject &state)
{
    state[u"since"_s] = state.value(u"since"_s).toInt() + 1;
}

bool Curiosity::due(const QJsonObject &state, qint64 nowMs)
{
    if (hasPending(state, nowMs))
        return false;
    const double last = state.value(u"last"_s).toDouble(0);
    // The very first question may come a little earlier: two messages in.
    const int needed = last > 0 ? kMessagesBetween : 2;
    return state.value(u"since"_s).toInt() >= needed && nowMs - qint64(last) >= kMinGapMs;
}

bool Curiosity::hasPending(const QJsonObject &state, qint64 nowMs)
{
    const QJsonObject p = state.value(u"pending"_s).toObject();
    return !p.isEmpty() && nowMs - qint64(p.value(u"at"_s).toDouble()) < kAnswerWindowMs;
}

Curiosity::Question Curiosity::pick(const QJsonArray &facts, const QJsonArray &topics, const QJsonObject &activity,
                                    const QJsonObject &state, Lang lang, qint64 nowMs)
{
    QList<Question> candidates;
    auto basic = [&](const QString &slot, const QString &ru, const QString &en) {
        if (!hasSlot(facts, slot))
            candidates.append({u"slot:"_s + slot, slot, {}, tr2(lang, ru, en)});
    };

    basic(u"name"_s, u"Кстати, как мне к вам обращаться?"_s, u"By the way, what should I call you?"_s);

    // Hours in one application: what is going on there?
    const QJsonArray apps = activity.value(u"today"_s).toObject().value(u"apps"_s).toArray();
    static const QSet<QString> interesting = {u"coding"_s, u"design"_s, u"office"_s, u"gaming"_s, u"terminal"_s};
    for (const auto &v : apps) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(u"name"_s).toString();
        if (a.value(u"seconds"_s).toDouble() < 30 * 60 || !interesting.contains(a.value(u"category"_s).toString())
            || mentioned(facts, name))
            continue;
        candidates.append({u"app:"_s + text::normalize(name), u"note"_s, name,
                           tr2(lang, u"Вижу, вы сегодня много времени провели в %1. Над чем работаете?"_s,
                                     u"I see you've spent a lot of time in %1 today. What are you working on?"_s).arg(name)});
        break;
    }

    basic(u"project"_s, u"Над чем вы сейчас работаете?"_s, u"What are you working on these days?"_s);

    for (const auto &v : topics) {
        const QJsonObject t = v.toObject();
        const QString word = t.value(u"word"_s).toString();
        if (t.value(u"count"_s).toInt() < 4 || word.size() < 3 || mentioned(facts, word))
            continue;
        candidates.append({u"topic:"_s + word, u"note"_s, word,
                           tr2(lang, u"Вы часто говорите о «%1». Это для работы или для души?"_s,
                                     u"You often bring up \"%1\". Is that for work or for fun?"_s).arg(word)});
        break;
    }

    basic(u"occupation"_s, u"А чем вы занимаетесь — работа, учёба?"_s, u"What do you do — work, study?"_s);
    basic(u"likes"_s, u"Чем любите заниматься в свободное время?"_s, u"What do you like to do in your free time?"_s);
    if (!hasSlot(facts, u"skill"_s) && activity.value(u"today"_s).toObject().value(u"categories"_s).toArray().size() > 0
        && activity.value(u"today"_s).toObject().value(u"categories"_s).toArray().first().toObject().value(u"id"_s).toString() == u"coding")
        basic(u"skill"_s, u"На каких языках программирования вы пишете?"_s, u"Which programming languages do you use?"_s);
    basic(u"goal"_s, u"Какая у вас сейчас главная цель — в работе или вообще?"_s,
          u"What's your main goal right now — at work or in general?"_s);
    basic(u"location"_s, u"Из какого вы города? Пригодится для погоды и времени."_s,
          u"Which city are you in? Handy for weather and time."_s);

    for (const Question &q : std::as_const(candidates))
        if (!recentlyAsked(state, q.id, nowMs))
            return q;
    return {};
}

void Curiosity::markAsked(QJsonObject &state, const Question &q, qint64 nowMs)
{
    QJsonObject asked = state.value(u"asked"_s).toObject();
    asked[q.id] = double(nowMs);
    // Bounded: forget the oldest entries beyond 100.
    while (asked.size() > 100) {
        QString oldest;
        double min = 1e18;
        for (auto it = asked.begin(); it != asked.end(); ++it)
            if (it.value().toDouble() < min) {
                min = it.value().toDouble();
                oldest = it.key();
            }
        asked.remove(oldest);
    }
    state[u"asked"_s] = asked;
    state[u"pending"_s] = QJsonObject{{u"id"_s, q.id}, {u"slot"_s, q.slot}, {u"subject"_s, q.subject},
                                      {u"text"_s, q.text}, {u"at"_s, double(nowMs)}};
    state[u"last"_s] = double(nowMs);
    state[u"since"_s] = 0;
}

bool Curiosity::takeAnswer(QJsonObject &state, const QString &message, qint64 nowMs, QString *slot, QString *value)
{
    if (!hasPending(state, nowMs)) {
        state.remove(u"pending"_s);
        return false;
    }
    const QJsonObject pending = state.value(u"pending"_s).toObject();
    state.remove(u"pending"_s);

    QString answer = message.simplified();
    // A question back, a refusal or a long unrelated message is not an answer.
    static const QRegularExpression refusal(
        u"^(?:не скажу|не хочу|неважно|потом|пропусти|skip|no thanks|not now|never mind|i'd rather not)\\b"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    // A request ("покажи погоду", "write a poem") changes the subject.
    static const QRegularExpression request(
        u"^(?:(?:jarvis|джарвис)[,\\s]+)?(?:расскажи|покажи|найди|открой|запусти|сделай|напиши|объясни|переведи|посчитай|"
        u"включи|выключи|помоги|дай|скажи|составь|придумай|проверь|как|что|почему|зачем|где|когда|сколько|какой|какая|какие|"
        u"tell|show|find|open|run|start|make|write|explain|translate|calculate|turn|help|give|say|create|check|"
        u"what|how|why|where|when|which|who|can you|could you|please)\\b"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (answer.isEmpty() || answer.endsWith(u'?') || answer.size() > 200 || refusal.match(answer).hasMatch()
        || request.match(answer).hasMatch())
        return false;
    while (!answer.isEmpty() && QString(u".!…"_s).contains(answer.back()))
        answer.chop(1);

    const QString s = pending.value(u"slot"_s).toString();
    if (s == u"name") {
        // "Богдан", "зови меня Бо", "I'm Alex": keep the last one or two words.
        static const QRegularExpression lead(
            u"^(?:меня зовут|зовите меня|зови меня|называй меня|я|my name is|call me|i am|i'm|it's)\\s+"_s,
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        answer.remove(lead);
        const QStringList words = answer.split(u' ', Qt::SkipEmptyParts);
        static const QRegularExpression letters(u"^[\\p{L}-]{2,30}$"_s, QRegularExpression::UseUnicodePropertiesOption);
        if (words.isEmpty() || words.size() > 2)
            return false;
        for (const QString &w : words)
            if (!letters.match(w).hasMatch())
                return false;
        answer = words.join(u' ');
        answer[0] = answer.at(0).toUpper();
    } else if (answer.split(u' ', Qt::SkipEmptyParts).size() > 30) {
        return false;
    }
    const QString subject = pending.value(u"subject"_s).toString();
    *slot = s;
    *value = subject.isEmpty() ? answer : subject + u": "_s + answer;
    return true;
}

} // namespace jarvis
