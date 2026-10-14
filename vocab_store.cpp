#include "vocab_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

#include "memory_text.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr double kDayMs = 24.0 * 3600 * 1000;
constexpr double kMinEase = 1.3;

QString clean(const QString &value)
{
    QString out = value.simplified();
    out.remove(QRegularExpression(u"[\\x00-\\x1f\\x7f]"_s));
    return out;
}

// Cards may go into Claude's prompt: same markup rule as the memory.
bool unsafe(const QString &value)
{
    return value.contains(u'<') || value.contains(u'>') || value.contains(u'{') || value.contains(u'}');
}

QDate dayOf(double ms)
{
    return QDateTime::fromMSecsSinceEpoch(qint64(ms)).date();
}

} // namespace

QString VocabStore::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/vocabulary.json"_s;
}

QJsonArray VocabStore::load() const
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 16 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(u"cards"_s).toArray();
}

bool VocabStore::save(const QJsonArray &cards) const
{
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(u"."_s))
        return false;
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    const QByteArray data = QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"cards"_s, cards}}).toJson(QJsonDocument::Compact);
    return f.write(data) == data.size() && f.commit();
}

QString VocabStore::add(const QString &frontInput, const QString &backInput, const QString &exampleInput,
                        const QString &source, qint64 now, QString *id)
{
    const QString front = clean(frontInput);
    const QString back = clean(backInput);
    const QString example = clean(exampleInput);
    if (text::normalize(front).isEmpty() || text::normalize(back).isEmpty())
        return u"empty"_s;
    if (front.size() > kMaxChars || back.size() > kMaxChars || example.size() > kMaxChars)
        return u"long"_s;
    if (text::looksSensitive(front + u' ' + back + u' ' + example) || unsafe(front + back + example))
        return u"sensitive"_s;
    QJsonArray cards = load();
    const QString key = text::normalize(front);
    for (const auto &v : std::as_const(cards))
        if (text::normalize(v.toObject().value(u"front"_s).toString()) == key)
            return u"duplicate"_s;
    if (cards.size() >= kMaxCards)
        return u"full"_s;
    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    cards.append(QJsonObject{
        {u"id"_s, newId}, {u"front"_s, front}, {u"back"_s, back}, {u"example"_s, example},
        {u"source"_s, source}, {u"created"_s, double(now)}, {u"due"_s, double(now)},
        {u"interval"_s, 0.0}, {u"ease"_s, 2.5}, {u"reps"_s, 0}, {u"lapses"_s, 0},
    });
    if (!save(cards))
        return u"io"_s;
    if (id)
        *id = newId;
    return {};
}

QString VocabStore::edit(const QString &id, const QString &backInput, const QString &exampleInput)
{
    const QString back = clean(backInput);
    const QString example = clean(exampleInput);
    if (text::normalize(back).isEmpty())
        return u"empty"_s;
    if (back.size() > kMaxChars || example.size() > kMaxChars)
        return u"long"_s;
    if (text::looksSensitive(back + u' ' + example) || unsafe(back + example))
        return u"sensitive"_s;
    QJsonArray cards = load();
    for (int i = 0; i < cards.size(); ++i) {
        QJsonObject c = cards.at(i).toObject();
        if (c.value(u"id"_s).toString() != id)
            continue;
        c[u"back"_s] = back;
        c[u"example"_s] = example;
        cards[i] = c;
        return save(cards) ? QString() : u"io"_s;
    }
    return u"empty"_s;
}

bool VocabStore::remove(const QString &id)
{
    QJsonArray cards = load();
    for (int i = 0; i < cards.size(); ++i)
        if (cards.at(i).toObject().value(u"id"_s).toString() == id) {
            cards.removeAt(i);
            return save(cards);
        }
    return false;
}

bool VocabStore::contains(const QString &front) const
{
    const QString key = text::normalize(front);
    for (const auto &v : load())
        if (text::normalize(v.toObject().value(u"front"_s).toString()) == key)
            return true;
    return false;
}

double VocabStore::plannedDays(const QJsonObject &card, int grade)
{
    constexpr double tenMinutes = 10.0 / (24 * 60);
    const int reps = card.value(u"reps"_s).toInt();
    const double interval = card.value(u"interval"_s).toDouble();
    const double ease = std::max(kMinEase, card.value(u"ease"_s).toDouble(2.5));
    double days = 0;
    switch (grade) {
    case Again:
        return tenMinutes;
    case Hard:
        days = reps == 0 ? 0.5 : reps == 1 ? 2 : std::max(interval + 1, interval * 1.2);
        break;
    case Good:
        days = reps == 0 ? 1 : reps == 1 ? 3 : std::max(interval + 1, interval * ease);
        break;
    default:
        days = reps == 0 ? 4 : reps == 1 ? 6 : std::max(interval + 2, interval * ease * 1.3);
        break;
    }
    return std::min(days, 3650.0);
}

bool VocabStore::review(const QString &id, int grade, qint64 now)
{
    if (grade < Again || grade > Easy)
        return false;
    QJsonArray cards = load();
    for (int i = 0; i < cards.size(); ++i) {
        QJsonObject c = cards.at(i).toObject();
        if (c.value(u"id"_s).toString() != id)
            continue;
        const double days = plannedDays(c, grade);
        double ease = c.value(u"ease"_s).toDouble(2.5);
        if (grade == Again) {
            c[u"lapses"_s] = c.value(u"lapses"_s).toInt() + 1;
            c[u"reps"_s] = 0;
            c[u"interval"_s] = 0.0;
            ease -= 0.2;
        } else {
            c[u"reps"_s] = c.value(u"reps"_s).toInt() + 1;
            c[u"interval"_s] = days;
            ease += grade == Hard ? -0.15 : grade == Easy ? 0.15 : 0.0;
        }
        c[u"ease"_s] = std::clamp(ease, kMinEase, 3.0);
        c[u"due"_s] = double(now) + days * kDayMs;
        c[u"reviewed"_s] = double(now);
        cards[i] = c;
        return save(cards);
    }
    return false;
}

QJsonArray VocabStore::cards() const
{
    QJsonArray out;
    const QJsonArray cards = load();
    for (int i = cards.size() - 1; i >= 0; --i)
        out.append(cards.at(i));
    return out;
}

QJsonObject VocabStore::next(qint64 now) const
{
    QJsonObject best;
    for (const auto &v : load()) {
        const QJsonObject c = v.toObject();
        const double due = c.value(u"due"_s).toDouble();
        if (due <= double(now) && (best.isEmpty() || due < best.value(u"due"_s).toDouble()))
            best = c;
    }
    if (!best.isEmpty())
        best[u"plan"_s] = QJsonArray{plannedDays(best, Again), plannedDays(best, Hard), plannedDays(best, Good),
                                     plannedDays(best, Easy)};
    return best;
}

QJsonObject VocabStore::stats(qint64 now) const
{
    int due = 0, learned = 0, fresh = 0, today = 0;
    const QJsonArray cards = load();
    const QDate day = dayOf(double(now));
    for (const auto &v : cards) {
        const QJsonObject c = v.toObject();
        due += c.value(u"due"_s).toDouble() <= double(now) ? 1 : 0;
        learned += c.value(u"interval"_s).toDouble() >= 21 ? 1 : 0;
        fresh += c.contains(u"reviewed"_s) ? 0 : 1;
        today += c.contains(u"reviewed"_s) && dayOf(c.value(u"reviewed"_s).toDouble()) == day ? 1 : 0;
    }
    return {{u"total"_s, cards.size()}, {u"due"_s, due}, {u"learned"_s, learned}, {u"fresh"_s, fresh},
            {u"reviewedToday"_s, today}};
}

QString VocabStore::promptSummary(qint64 now, int maxChars) const
{
    const QJsonArray cards = load();
    if (cards.isEmpty())
        return {};
    const QJsonObject s = stats(now);
    QString out = u"The user studies French with flashcards: %1 cards, %2 due for review, %3 learned.\n"_s
                      .arg(s.value(u"total"_s).toInt())
                      .arg(s.value(u"due"_s).toInt())
                      .arg(s.value(u"learned"_s).toInt());
    // Words that keep slipping: most lapses, then lowest ease.
    QList<QJsonObject> hard;
    for (const auto &v : cards) {
        const QJsonObject c = v.toObject();
        if (c.value(u"lapses"_s).toInt() > 0)
            hard.append(c);
    }
    std::sort(hard.begin(), hard.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(u"lapses"_s).toInt() != b.value(u"lapses"_s).toInt()
                   ? a.value(u"lapses"_s).toInt() > b.value(u"lapses"_s).toInt()
                   : a.value(u"ease"_s).toDouble() < b.value(u"ease"_s).toDouble();
    });
    QStringList recent;
    for (int i = cards.size() - 1; i >= 0 && recent.size() < 8; --i)
        recent.append(cards.at(i).toObject().value(u"front"_s).toString());
    if (!hard.isEmpty()) {
        QStringList words;
        for (int i = 0; i < hard.size() && i < 8; ++i)
            words.append(hard.at(i).value(u"front"_s).toString() + u" — "_s + hard.at(i).value(u"back"_s).toString());
        out += u"Words the user struggles with: "_s + words.join(u"; "_s) + u'\n';
    }
    out += u"Recently added: "_s + recent.join(u", "_s) + u'\n';
    return out.left(maxChars);
}

} // namespace jarvis
