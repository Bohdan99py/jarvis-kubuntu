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
    // "", "daily", "weekdays", "weekends", "weekly", "monthly", "yearly",
    // "days:1,3,5" (ISO weekdays) or "every:30m" / "every:2h" / "every:3d".
    QString repeat;
};

// What vague words mean for this user ("утром", "вечером", a day without a time).
struct ReminderOptions
{
    int morning = 9;
    int afternoon = 14;
    int evening = 19;
};

class ReminderParser
{
public:
    static ParsedReminder parse(const QString &input, const QDateTime &now, const ReminderOptions &options = {});
    static bool validRepeat(const QString &repeat);

    // "today at 18:30", "tomorrow at 09:00", "on Friday at 10:00", "in 20 min (18:05)".
    static QString describeWhen(const QDateTime &due, const QDateTime &now, Lang lang);
    // "every day", "on weekdays", "every Monday", "every 2 hours"; "" for one-time reminders.
    static QString describeRepeat(const QString &repeat, const QDateTime &due, Lang lang);
    // The next occurrence of a repeating reminder strictly after `after`.
    static QDateTime nextOccurrence(const QDateTime &due, const QString &repeat, const QDateTime &after);
};

} // namespace jarvis
