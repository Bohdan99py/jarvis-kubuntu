#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "language.h"

namespace jarvis {

// Reminders and to-do items. A reminder has a due time (and maybe a
// repetition); a task has none and stays open until it is done.
//   {"id", "text", "due": ms (0 = task), "repeat": ""|"daily"|"weekdays"|"weekly",
//    "created": ms, "done": ms (0 = open), "fired": ms of the last notification}
// ${XDG_DATA_HOME}/jarvis/tasks.json, file 0600, atomic writes; every call
// re-reads the file, so the daemon and a local-mode GUI agree.
class TaskStore
{
public:
    static constexpr int kMaxTasks = 500;
    static constexpr int kMaxChars = 300;
    static constexpr qint64 kKeepDoneMs = 30LL * 24 * 3600 * 1000;

    // Returns "" or an error code: "short", "long", "sensitive", "unsafe", "full", "io".
    QString add(const QString &text, qint64 due, const QString &repeat, qint64 now, QString *id = nullptr);
    bool complete(const QString &id, qint64 now);
    bool remove(const QString &id);
    // Moves a reminder (or turns a task into one) to `until` and reopens it.
    bool snooze(const QString &id, qint64 until);
    // Changes the text, time and repetition of an item; due 0 makes it a plain task.
    // Returns "" or an error code like add().
    QString update(const QString &id, const QString &text, qint64 due, const QString &repeat);
    // Open items whose text matches `query` by words; for "cancel the reminder about …".
    QList<QJsonObject> find(const QString &query) const;

    // Open items first (reminders by due time, then tasks), then recently done ones.
    QJsonArray tasks() const;
    QJsonObject task(const QString &id) const;
    // Open reminders that are due and not announced yet.
    QList<QJsonObject> due(qint64 now) const;
    // A due reminder was announced: one-time reminders are done (or, with
    // `againMs`, asked again up to kMaxNags times until done), repeating ones move on.
    void fired(const QString &id, qint64 now, qint64 againMs = 0);
    static constexpr int kMaxNags = 3;
    // Earliest due time of an open reminder, 0 when there is none.
    qint64 nextDue() const;
    int openCount() const;

    // "Reminders:\n• tomorrow at 09:00 — call mum\nTasks:\n• buy solder" for the chat.
    QString describe(Lang lang, qint64 now) const;
    // English block for Claude's context; "" when empty.
    QString promptSummary(qint64 now, int maxItems = 8) const;

private:
    static QString path();
    QJsonArray load() const;
    bool save(const QJsonArray &tasks) const;
};

} // namespace jarvis
