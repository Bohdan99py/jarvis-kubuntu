#include "reminder_parser.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

QRegularExpression rx(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption
                                           | QRegularExpression::UseUnicodePropertiesOption);
}

const QString kWeekdays = u"понедельник|вторник|среду|среда|четверг|пятницу|пятница|субботу|суббота|воскресенье|"
                          u"monday|tuesday|wednesday|thursday|friday|saturday|sunday"_s;
const QString kMonths = u"января|февраля|марта|апреля|мая|июня|июля|августа|сентября|октября|ноября|декабря|"
                        u"january|february|march|april|may|june|july|august|september|october|november|december|"
                        u"jan|feb|mar|apr|jun|jul|aug|sept|sep|oct|nov|dec"_s;
const QString kNumberWords = u"одну|один|одна|две|два|пару|три|четыре|пять|шесть|семь|восемь|девять|десять|"
                             u"пятнадцать|двадцать|тридцать|сорок|пятьдесят|несколько|a\\s+couple\\s+of|a\\s+few|"
                             u"an|a|one|two|three|four|five|six|seven|eight|nine|ten|fifteen|twenty|thirty|forty|fifty"_s;
// Ends a token: whitespace, punctuation or the end of the text.
const QString kEnd = u"(?=$|[\\s,.;:!?)])"_s;

int numberWord(const QString &word)
{
    static const QHash<QString, int> words = {
        {u"одну"_s, 1}, {u"один"_s, 1}, {u"одна"_s, 1}, {u"две"_s, 2}, {u"два"_s, 2}, {u"пару"_s, 2},
        {u"три"_s, 3}, {u"четыре"_s, 4}, {u"пять"_s, 5}, {u"шесть"_s, 6}, {u"семь"_s, 7}, {u"восемь"_s, 8},
        {u"девять"_s, 9}, {u"десять"_s, 10}, {u"пятнадцать"_s, 15}, {u"двадцать"_s, 20}, {u"тридцать"_s, 30},
        {u"сорок"_s, 40}, {u"пятьдесят"_s, 50}, {u"несколько"_s, 3}, {u"a couple of"_s, 2}, {u"a few"_s, 3},
        {u"an"_s, 1}, {u"a"_s, 1}, {u"one"_s, 1}, {u"two"_s, 2}, {u"three"_s, 3}, {u"four"_s, 4}, {u"five"_s, 5},
        {u"six"_s, 6}, {u"seven"_s, 7}, {u"eight"_s, 8}, {u"nine"_s, 9}, {u"ten"_s, 10}, {u"fifteen"_s, 15},
        {u"twenty"_s, 20}, {u"thirty"_s, 30}, {u"forty"_s, 40}, {u"fifty"_s, 50}};
    return words.value(word.simplified().toLower(), -1);
}

// 1 = Monday … 7 = Sunday, 0 when unknown.
int weekdayIndex(const QString &word)
{
    static const QStringList prefixes = {u"пон"_s, u"вт"_s, u"ср"_s, u"чет"_s, u"пят"_s, u"суб"_s, u"вос"_s,
                                         u"mon"_s, u"tue"_s, u"wed"_s, u"thu"_s, u"fri"_s, u"sat"_s, u"sun"_s};
    const QString w = word.toLower();
    for (int i = 0; i < prefixes.size(); ++i)
        if (w.startsWith(prefixes.at(i)))
            return i % 7 + 1;
    return 0;
}

int monthIndex(const QString &word)
{
    static const QStringList prefixes = {u"янв"_s, u"фев"_s, u"мар"_s, u"апр"_s, u"мая"_s, u"июн"_s,
                                         u"июл"_s, u"авг"_s, u"сен"_s, u"окт"_s, u"ноя"_s, u"дек"_s,
                                         u"jan"_s, u"feb"_s, u"mar"_s, u"apr"_s, u"may"_s, u"jun"_s,
                                         u"jul"_s, u"aug"_s, u"sep"_s, u"oct"_s, u"nov"_s, u"dec"_s};
    const QString w = word.toLower();
    for (int i = 0; i < prefixes.size(); ++i)
        if (w.startsWith(prefixes.at(i)))
            return i % 12 + 1;
    return 0;
}

// The working copy: matched spans are blanked in the lowercase text (so later
// patterns cannot match them again) and in the original (so the rest is "what").
struct Work
{
    QString lower;
    QString original;

    QRegularExpressionMatch take(const QRegularExpression &re)
    {
        const QRegularExpressionMatch m = re.match(lower);
        if (m.hasMatch())
            for (int i = m.capturedStart(); i < m.capturedEnd(); ++i) {
                lower[i] = u' ';
                original[i] = u' ';
            }
        return m;
    }
};

int hourWithSuffix(int hour, const QString &suffix)
{
    const QString s = suffix.toLower().remove(u'.');
    if (s == u"утра" || s == u"am")
        return hour == 12 ? 0 : hour;
    if (s == u"дня" || s == u"вечера" || s == u"pm")
        return hour < 12 ? hour + 12 : hour;
    if (s == u"ночи")
        return hour == 12 ? 0 : (hour >= 6 && hour < 12 ? hour + 12 : hour);
    return hour;
}

QString cleanText(QString text)
{
    static const QRegularExpression lead = rx(
        u"^[\\s,:;—–-]*(?:(?:о\\s+том|about),?\\s*(?:что(?:бы)?|that)?|что(?:бы)?|про|насч[её]т|to|that|about)(?=\\s|$)[\\s,:]*"_s);
    static const QRegularExpression dangling = rx(u"(?:^|\\s)(?:в|во|к|на|at|on|by|in)\\s*$"_s);
    static const QRegularExpression tail = rx(u"[\\s,.;:!—–-]+$"_s);
    text = text.simplified();
    text.remove(lead);
    for (int i = 0; i < 2; ++i) {
        text.remove(tail);
        text.remove(dangling);
    }
    return text.simplified();
}

} // namespace

ParsedReminder ReminderParser::parse(const QString &input, const QDateTime &now)
{
    ParsedReminder result;
    if (input.size() > 1000)
        return result;
    static const QRegularExpression reminderTrigger = rx(
        u"^\\s*(?:(?:джарвис|jarvis)[,!\\s]+)?(?:(?:пожалуйста|please)[,\\s]+)?"
        u"(?:(?:можешь|could\\s+you|can\\s+you|would\\s+you)\\s+)?"
        u"(?:напомина(?:й|йте)|напомни(?:те|ть)?|постав(?:ь|и)\\s+напоминание|созда(?:й|ть)\\s+напоминание|установи\\s+напоминание|"
        u"remind|set\\s+(?:a\\s+|an\\s+)?reminder|create\\s+(?:a\\s+)?reminder)"
        u"(?:\\s+(?:мне|нам|me|us))?(?=\\s|$|[,.:!?])[\\s,:]*"_s);
    static const QRegularExpression taskTrigger = rx(
        u"^\\s*(?:(?:джарвис|jarvis)[,!\\s]+)?(?:(?:добавь|запиши|создай|внеси)\\s+(?:в\\s+)?"
        u"(?:новую\\s+)?(?:задачу|задачи|список\\s+дел|дела|туду|todo)|(?:add|create|put)\\s+(?:a\\s+|an\\s+)?(?:new\\s+)?"
        u"(?:task|todo|to-do)|add\\s+to\\s+(?:my\\s+)?(?:tasks|todo(?:\\s+list)?|to-do(?:\\s+list)?))(?=\\s|$|[,.:!?])"
        u"[\\s,:—–-]*"_s);

    Work w{input.toLower(), input};
    bool task = false;
    if (!w.take(reminderTrigger).hasMatch()) {
        if (!w.take(taskTrigger).hasMatch())
            return result;
        task = true;
    }

    // ---- Repetition ----
    int weekday = 0;     // 1..7
    int partHour = -1;   // "утром", "every evening"
    if (w.take(rx(u"\\b(?:каждый\\s+день|ежедневно|every\\s+day|each\\s+day|daily)\\b"_s)).hasMatch()) {
        result.repeat = u"daily"_s;
    } else if (w.take(rx(u"\\b(?:каждое\\s+утро|every\\s+morning)\\b"_s)).hasMatch()) {
        result.repeat = u"daily"_s;
        partHour = 9;
    } else if (w.take(rx(u"\\b(?:каждый\\s+вечер|every\\s+evening|every\\s+night)\\b"_s)).hasMatch()) {
        result.repeat = u"daily"_s;
        partHour = 19;
    } else if (w.take(rx(u"\\b(?:по\\s+будням|в\\s+будни|каждый\\s+будний\\s+день|on\\s+weekdays|every\\s+weekday|weekdays)\\b"_s)).hasMatch()) {
        result.repeat = u"weekdays"_s;
    } else if (const auto m = w.take(rx(u"\\b(?:каждый|каждую|каждое|every)\\s+("_s + kWeekdays + u")\\b"_s)); m.hasMatch()) {
        result.repeat = u"weekly"_s;
        weekday = weekdayIndex(m.captured(1));
    } else if (w.take(rx(u"\\b(?:каждую\\s+неделю|еженедельно|every\\s+week|weekly)\\b"_s)).hasMatch()) {
        result.repeat = u"weekly"_s;
    }

    // ---- Relative time: "через 20 минут", "in an hour" ----
    static const QRegularExpression relative = rx(
        u"\\b(?:через|in)\\s+(?:(\\d+(?:[.,]\\d+)?)\\s*|("_s + kNumberWords + u")\\s+)?"
        u"(полчаса|полтора\\s+часа|half\\s+an\\s+hour|минут[уы]?|мин\\.?|minutes?|mins?|час(?:а|ов)?|ч\\.?|hours?|hrs?|"
        u"дн(?:я|ей)|день|сутки|days?|недел[юиь]|weeks?|секунд[уы]?|сек\\.?|seconds?|secs?)"_s + kEnd);
    qint64 delta = -1;
    if (const auto m = w.take(relative); m.hasMatch()) {
        double n = 1;
        if (!m.captured(1).isEmpty())
            n = QString(m.captured(1)).replace(u',', u'.').toDouble();
        else if (!m.captured(2).isEmpty())
            n = numberWord(m.captured(2));
        const QString unit = m.captured(3).toLower();
        qint64 seconds = 0;
        if (unit == u"полчаса" || unit.startsWith(u"half"))
            seconds = 1800;
        else if (unit.startsWith(u"полтора"))
            seconds = 5400;
        else if (unit.startsWith(u"сек") || unit.startsWith(u"sec"))
            seconds = qint64(n);
        else if (unit.startsWith(u"мин") || unit.startsWith(u"min"))
            seconds = qint64(n * 60);
        else if (unit.startsWith(u"ч") || unit.startsWith(u"hour") || unit.startsWith(u"hr"))
            seconds = qint64(n * 3600);
        else if (unit.startsWith(u"нед") || unit.startsWith(u"week"))
            seconds = qint64(n * 7 * 86400);
        else
            seconds = qint64(n * 86400);
        if (seconds > 0 && seconds <= 366LL * 86400)
            delta = seconds;
    }

    // ---- Day ----
    int dayOffset = -1;
    QDate explicitDate;
    bool explicitYear = false;
    bool tonight = false;
    if (w.take(rx(u"\\b(?:послезавтра|day\\s+after\\s+tomorrow)\\b"_s)).hasMatch()) {
        dayOffset = 2;
    } else if (w.take(rx(u"\\b(?:завтра|tomorrow)\\b"_s)).hasMatch()) {
        dayOffset = 1;
    } else if (const auto m = w.take(rx(u"\\b(сегодня|today|tonight)\\b"_s)); m.hasMatch()) {
        dayOffset = 0;
        tonight = m.captured(1).toLower() == u"tonight";
    }
    if (weekday == 0 && dayOffset < 0) {
        if (const auto m = w.take(rx(u"\\b(?:(?:в|во|on|next|this)\\s+)?("_s + kWeekdays + u")\\b"_s)); m.hasMatch())
            weekday = weekdayIndex(m.captured(1));
    }
    if (dayOffset < 0 && weekday == 0) {
        if (const auto m = w.take(rx(u"\\b(\\d{1,2})[./](\\d{1,2})(?:[./](\\d{4}|\\d{2}))?\\b"_s)); m.hasMatch()) {
            int year = now.date().year();
            if (!m.captured(3).isEmpty()) {
                year = m.captured(3).toInt();
                if (year < 100)
                    year += 2000;
                explicitYear = true;
            }
            explicitDate = QDate(year, m.captured(2).toInt(), m.captured(1).toInt());
        } else if (const auto m = w.take(rx(u"\\b(\\d{1,2})\\s+("_s + kMonths + u")\\b"_s)); m.hasMatch()) {
            explicitDate = QDate(now.date().year(), monthIndex(m.captured(2)), m.captured(1).toInt());
        } else if (const auto m = w.take(rx(u"\\b(?:on\\s+)?("_s + kMonths + u")\\s+(\\d{1,2})(?:st|nd|rd|th)?\\b"_s)); m.hasMatch()) {
            explicitDate = QDate(now.date().year(), monthIndex(m.captured(1)), m.captured(2).toInt());
        }
        if (explicitDate.isValid() && !explicitYear && explicitDate < now.date())
            explicitDate = explicitDate.addYears(1);
    }

    // ---- Time of day ----
    int hour = -1, minute = 0;
    bool suffix = false;
    static const QRegularExpression atTime = rx(
        u"\\b(?:в|к|около|at|by|around)\\s+(\\d{1,2})(?:[:.](\\d{2}))?(?:\\s*час(?:а|ов)?)?(?:\\s+(\\d{1,2})\\s*мин(?:ут[уы]?)?)?"
        u"(?:\\s*(утра|дня|вечера|ночи|am|pm|a\\.m\\.|p\\.m\\.))?"_s + kEnd);
    static const QRegularExpression clock = rx(u"\\b(\\d{1,2}):(\\d{2})(?:\\s*(am|pm))?"_s + kEnd);
    static const QRegularExpression ampm = rx(u"\\b(\\d{1,2})\\s*(am|pm|a\\.m\\.|p\\.m\\.|утра|вечера)"_s + kEnd);
    QRegularExpressionMatch tm = w.take(atTime);
    if (!tm.hasMatch())
        tm = w.take(clock);
    if (tm.hasMatch()) {
        hour = tm.captured(1).toInt();
        minute = tm.captured(2).isEmpty() ? (tm.lastCapturedIndex() >= 3 ? tm.captured(3).toInt() : 0) : tm.captured(2).toInt();
        const QString sfx = tm.lastCapturedIndex() >= 4 ? tm.captured(4) : tm.captured(3);
        if (!sfx.isEmpty() && !sfx.at(0).isDigit()) {
            hour = hourWithSuffix(hour, sfx);
            suffix = true;
        }
    } else if (const auto m = w.take(ampm); m.hasMatch()) {
        hour = hourWithSuffix(m.captured(1).toInt(), m.captured(2));
        suffix = true;
    } else if (w.take(rx(u"\\b(?:в\\s+|at\\s+)?(?:полдень|noon)\\b"_s)).hasMatch()) {
        hour = 12;
        suffix = true;
    } else if (w.take(rx(u"\\b(?:в\\s+|at\\s+)?(?:полночь|midnight)\\b"_s)).hasMatch()) {
        hour = 0;
        suffix = true;
        if (dayOffset < 0 && weekday == 0 && !explicitDate.isValid())
            dayOffset = 1;
    }
    if (hour > 23 || minute > 59)
        return result; // "в 25:00" is not a time: let the chat answer
    if (const auto m = w.take(rx(u"\\b(утром|с\\s+утра|in\\s+the\\s+morning|this\\s+morning|днём|днем|"
                                 u"in\\s+the\\s+afternoon|this\\s+afternoon|вечером|in\\s+the\\s+evening|this\\s+evening|"
                                 u"ночью|at\\s+night)\\b"_s));
        m.hasMatch() && partHour < 0) {
        const QString p = m.captured(1).toLower();
        partHour = p.contains(u"утр") || p.contains(u"morning") ? 9
                   : p.startsWith(u"дн") || p.contains(u"afternoon") ? 14
                   : p.contains(u"веч") || p.contains(u"evening") ? 19 : 22;
    }
    if (tonight && partHour < 0)
        partHour = 20;

    const QString rest = w.original.simplified().remove(rx(u"^[\\s,:;—–-]+"_s));
    result.text = cleanText(w.original);
    const bool timed = delta >= 0 || hour >= 0 || dayOffset >= 0 || weekday > 0 || explicitDate.isValid()
                       || partHour >= 0 || !result.repeat.isEmpty();
    if (!timed) {
        if (task) {
            result.kind = result.text.isEmpty() ? ParsedReminder::None : ParsedReminder::Task;
            return result;
        }
        // "напомни, что такое UART" asks to recall knowledge, not to set a reminder.
        static const QRegularExpression question = rx(
            u"^(?:что\\s+такое|как|какой|какая|какое|какие|где|кто|сколько|почему|зачем|когда|чем|"
            u"what|how|who|where|which|why|when)\\b"_s);
        if (input.trimmed().endsWith(u'?') || question.match(rest).hasMatch())
            return result;
        result.kind = ParsedReminder::NeedTime;
        return result;
    }

    if (delta >= 0) {
        result.due = now.addSecs(delta);
        result.repeat.clear();
        result.kind = ParsedReminder::Reminder;
        return result;
    }

    QDate date = now.date();
    bool fixed = false;
    if (dayOffset >= 0) {
        date = date.addDays(dayOffset);
        fixed = dayOffset > 0;
    } else if (weekday > 0) {
        date = date.addDays((weekday - date.dayOfWeek() + 7) % 7);
    } else if (explicitDate.isValid()) {
        date = explicitDate;
        fixed = true;
    } else if (hour < 0 && partHour < 0 && result.repeat.isEmpty()) {
        result.kind = ParsedReminder::NeedTime;
        return result;
    }

    QTime time;
    if (hour >= 0)
        time = QTime(hour, minute);
    else if (partHour >= 0)
        time = QTime(partHour, 0);
    else if (dayOffset == 0)
        // "напомни сегодня …": in the evening, or in an hour when the evening has begun.
        time = now.time() < QTime(18, 0) ? QTime(19, 0) : QTime(now.time().hour(), 0).addSecs(2 * 3600);
    else
        time = QTime(9, 0);
    if (dayOffset == 0 && hour < 0 && partHour < 0 && now.time() >= QTime(22, 0))
        date = date.addDays(1);

    QDateTime due(date, time);
    if (due <= now && !fixed) {
        // "в 7" said at three in the afternoon means seven in the evening.
        const QDateTime evening = due.addSecs(12 * 3600);
        if (hour >= 1 && hour <= 11 && !suffix && result.repeat.isEmpty() && evening > now
            && evening.date() == due.date())
            due = evening;
        else if (weekday > 0)
            due = due.addDays(7);
        else if (dayOffset == 0) {
            result.kind = ParsedReminder::NeedTime; // "сегодня в 9" at ten: that time has passed
            return result;
        } else
            due = due.addDays(1);
    }
    if (result.repeat == u"weekdays")
        while (due.date().dayOfWeek() > 5)
            due = due.addDays(1);
    if (due <= now) {
        result.kind = ParsedReminder::NeedTime;
        return result;
    }
    result.due = due;
    result.kind = ParsedReminder::Reminder;
    return result;
}

QDateTime ReminderParser::nextOccurrence(const QDateTime &due, const QString &repeat, const QDateTime &after)
{
    if (repeat.isEmpty() || !due.isValid())
        return {};
    QDateTime next = due;
    const int step = repeat == u"weekly" ? 7 : 1;
    // Jump close to `after` first, so a long-offline daemon does not loop for years.
    const qint64 behind = next.date().daysTo(after.date());
    if (behind > 7)
        next = next.addDays((behind / step - 1) * step);
    for (int guard = 0; guard < 30 && (next <= after || (repeat == u"weekdays" && next.date().dayOfWeek() > 5)); ++guard)
        next = next.addDays(step);
    return next;
}

QString ReminderParser::describeWhen(const QDateTime &due, const QDateTime &now, Lang lang)
{
    const bool ru = lang == Lang::Ru;
    const QString hhmm = due.toString(u"HH:mm"_s);
    const qint64 minutes = now.secsTo(due) / 60;
    if (now.secsTo(due) >= 0 && minutes < 60)
        return (ru ? u"через %1 мин (в %2)"_s : u"in %1 min (at %2)"_s).arg(std::max<qint64>(1, minutes)).arg(hhmm);
    const qint64 days = now.date().daysTo(due.date());
    if (days == 0)
        return (ru ? u"сегодня в %1"_s : u"today at %1"_s).arg(hhmm);
    if (days == 1)
        return (ru ? u"завтра в %1"_s : u"tomorrow at %1"_s).arg(hhmm);
    if (days > 1 && days < 7) {
        static const QStringList ruDays = {u"в понедельник"_s, u"во вторник"_s, u"в среду"_s, u"в четверг"_s,
                                           u"в пятницу"_s, u"в субботу"_s, u"в воскресенье"_s};
        if (ru)
            return ruDays.at(due.date().dayOfWeek() - 1) + u" в "_s + hhmm;
        return u"on %1 at %2"_s.arg(QLocale(QLocale::English).dayName(due.date().dayOfWeek()), hhmm);
    }
    const QLocale locale(ru ? QLocale::Russian : QLocale::English);
    const QString format = due.date().year() == now.date().year() ? u"d MMMM"_s : u"d MMMM yyyy"_s;
    return (ru ? u"%1 в %2"_s : u"on %1 at %2"_s).arg(locale.toString(due.date(), format), hhmm);
}

QString ReminderParser::describeRepeat(const QString &repeat, const QDateTime &due, Lang lang)
{
    const bool ru = lang == Lang::Ru;
    if (repeat == u"daily")
        return ru ? u"каждый день"_s : u"every day"_s;
    if (repeat == u"weekdays")
        return ru ? u"по будням"_s : u"on weekdays"_s;
    if (repeat == u"weekly") {
        static const QStringList ruDays = {u"каждый понедельник"_s, u"каждый вторник"_s, u"каждую среду"_s,
                                           u"каждый четверг"_s, u"каждую пятницу"_s, u"каждую субботу"_s,
                                           u"каждое воскресенье"_s};
        const int day = due.isValid() ? due.date().dayOfWeek() : 1;
        return ru ? ruDays.at(day - 1) : u"every "_s + QLocale(QLocale::English).dayName(day);
    }
    return {};
}

} // namespace jarvis
