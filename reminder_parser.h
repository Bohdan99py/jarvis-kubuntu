#pragma once

#include <QDateTime>
#include <QString>

#include "language.h"

namespace jarvis {

// Reads reminders and to-do items from plain Russian or English phrases:
// "напомни завтра в 9 позвонить маме", "через 20 минут проверь сборку",
// "remind me every Monday at 10 to send the report", "добавь задачу купить
// паяльник". No network: what the rules cannot place in time is left to Claude.
struct ParsedReminder
{
    enum Kind {
        None,     // not a reminder or a task
        Reminder, // `due` is set
        NeedTime, // "remind me …" without a time the rules understand
        Task,     // a to-do item without a time
    };
    Kind kind = None;
    QString text;   // what to remind about, without the trigger and the time
    QDateTime due;  // local time; invalid for tasks
    QString repeat; // "", "daily", "weekdays" or "weekly"
};

class ReminderParser
{
public:
    static ParsedReminder parse(const QString &input, const QDateTime &now);

    // "today at 18:30", "tomorrow at 09:00", "on Friday at 10:00", "in 20 min (18:05)".
    static QString describeWhen(const QDateTime &due, const QDateTime &now, Lang lang);
    // "every day", "on weekdays", "every Monday"; "" for one-time reminders.
    static QString describeRepeat(const QString &repeat, const QDateTime &due, Lang lang);
    // The next occurrence of a repeating reminder strictly after `after`.
    static QDateTime nextOccurrence(const QDateTime &due, const QString &repeat, const QDateTime &after);
};

} // namespace jarvis
