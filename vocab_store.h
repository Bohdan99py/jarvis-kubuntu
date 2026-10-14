#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace jarvis {

// Flashcards for language study (French → Russian) with spaced repetition:
// a simplified SM-2 — every answer moves the card's next review further away
// (or back to minutes after "again"), scaled by a per-card ease.
// ${XDG_DATA_HOME}/jarvis/vocabulary.json, file 0600, atomic writes; every call
// re-reads the file, so the GUI and the daemon (which only reads it) agree.
class VocabStore
{
public:
    static constexpr int kMaxCards = 5000;
    static constexpr int kMaxChars = 300;

    enum Grade { Again = 0, Hard = 1, Good = 2, Easy = 3 };

    // Sources: "manual", "lesson" (notes of a recording), "translation", "pdf".
    // Returns "" or an error code: "empty", "long", "sensitive", "duplicate",
    // "full", "io". `id` receives the new card's id.
    QString add(const QString &front, const QString &back, const QString &example, const QString &source,
                qint64 now, QString *id = nullptr);
    // Fixes a card's translation or example.
    QString edit(const QString &id, const QString &back, const QString &example);
    bool remove(const QString &id);
    bool contains(const QString &front) const;
    bool review(const QString &id, int grade, qint64 now);

    // Newest first.
    QJsonArray cards() const;
    // The most overdue card due now, {} when nothing is due. Each card carries
    // "plan": the delay in days that every grade would give, for the buttons.
    QJsonObject next(qint64 now) const;
    // total, due, learned (interval of three weeks or more), fresh (never reviewed), reviewedToday
    QJsonObject stats(qint64 now) const;
    // For Claude: deck size, due cards and the words the user struggles with.
    QString promptSummary(qint64 now, int maxChars = 600) const;

    // Delay in days after answering `card` with `grade` (minutes are fractions).
    static double plannedDays(const QJsonObject &card, int grade);

private:
    static QString path();
    QJsonArray load() const;
    bool save(const QJsonArray &cards) const;
};

} // namespace jarvis
