#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "language.h"

namespace jarvis {

// Jarvis's curiosity: questions that fill gaps in what it knows about the
// user — the basics (name, work, projects, hobbies), applications the user
// spends hours in, topics that keep coming up. It asks rarely (every few
// messages, at most every ten minutes, each question once a month) and treats
// the next plain message after a question as the answer.
//
// Stateless functions over a small JSON state kept in knowledge.json:
//   {"asked": {id: ms}, "pending": {id, slot, subject, at}, "last": ms, "since": n}
class Curiosity
{
public:
    static constexpr int kMessagesBetween = 3;
    static constexpr qint64 kMinGapMs = 10 * 60 * 1000;
    static constexpr qint64 kRepeatAfterMs = 30LL * 24 * 3600 * 1000;
    static constexpr qint64 kAnswerWindowMs = 15 * 60 * 1000;

    struct Question
    {
        QString id;
        QString slot;
        QString subject; // app or topic the question is about, if any
        QString text;
        bool isValid() const { return !id.isEmpty(); }
    };

    // A user message that was not a command: counts towards the next question.
    static void countMessage(QJsonObject &state);
    static bool due(const QJsonObject &state, qint64 nowMs);

    // The most useful question not asked recently, or an invalid Question.
    static Question pick(const QJsonArray &facts, const QJsonArray &topics, const QJsonObject &activity,
                         const QJsonObject &state, Lang lang, qint64 nowMs);
    static void markAsked(QJsonObject &state, const Question &q, qint64 nowMs);

    // When a question is pending and `message` reads like its answer, returns
    // true with the slot and value to store. The pending question is cleared
    // either way once the user has replied.
    static bool takeAnswer(QJsonObject &state, const QString &message, qint64 nowMs,
                           QString *slot, QString *value);
    static bool hasPending(const QJsonObject &state, qint64 nowMs);
};

} // namespace jarvis
