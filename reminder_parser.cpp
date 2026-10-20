#include "reminder_parser.h"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

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

// "понедельникам", "пн", "mondays", "mon" → 1..7.
int weekdayAny(const QString &word)
{
    static const QStringList ru = {u"пн"_s, u"вт"_s, u"ср"_s, u"чт"_s, u"пт"_s, u"сб"_s, u"вс"_s};
    const QString w = word.toLower();
    if (const int i = int(ru.indexOf(w)); i >= 0)
        return i + 1;
    return weekdayIndex(w);
}

const QString kWeekdayAny = u"понедельник(?:ам|а|и)?|вторник(?:ам|а|и)?|сред(?:ам|у|а|ы)|четверг(?:ам|а|и)?|"
                            u"пятниц(?:ам|у|а|ы)|суббот(?:ам|у|а|ы)|воскресень(?:ям|е|я)|пн|вт|ср|чт|пт|сб|вс|"
                            u"mondays?|tuesdays?|wednesdays?|thursdays?|fridays?|saturdays?|sundays?|mon|tue|wed|thu|fri|sat|sun"_s;

// Does a day-filtered repetition fire on `day`?
bool firesOn(const QString &repeat, const QDate &day)
{
    const int dow = day.dayOfWeek();
    if (repeat == u"weekdays")
        return dow <= 5;
    if (repeat == u"weekends")
        return dow >= 6;
    if (repeat.startsWith(u"days:"))
        return repeat.mid(5).split(u',').contains(QString::number(dow));
    return true;
}

qint64 intervalSeconds(const QString &repeat)
{
    static const QRegularExpression every(u"^every:(\\d{1,4})([mhd])$"_s);
    const auto m = every.match(repeat);
    if (!m.hasMatch())
        return 0;
    const qint64 n = m.captured(1).toLongLong();
    return n * (m.captured(2) == u"m" ? 60 : m.captured(2) == u"h" ? 3600 : 86400);
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

bool ReminderParser::validRepeat(const QString &repeat)
{
    static const QStringList fixed = {u""_s, u"daily"_s, u"weekdays"_s, u"weekends"_s, u"weekly"_s, u"monthly"_s, u"yearly"_s};
    if (fixed.contains(repeat))
        return true;
    static const QRegularExpression days(u"^days:[1-7](?:,[1-7]){0,6}$"_s);
    if (days.match(repeat).hasMatch())
        return true;
    const qint64 step = intervalSeconds(repeat);
    return step >= 5 * 60 && step <= 366LL * 86400;
}

ParsedReminder ReminderParser::parse(const QString &input, const QDateTime &now, const ReminderOptions &options)
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
    int monthDay = 0;    // "каждое 5 число"
    static const QRegularExpression interval = rx(
        u"\\b(?:каждые|каждый|каждую|every)\\s+(?:(\\d{1,4})\\s*|(other)\\s+)?(полчаса|минут[уы]?|мин\\.?|час(?:а|ов)?|"
        u"дн(?:я|ей)|minutes?|mins?|hours?|days?)"_s + kEnd);
    static const QRegularExpression daySet = rx(
        u"\\b(?:по|каждый|каждую|каждое|every|on)\\s+((?:"_s + kWeekdayAny + u")(?:\\s*(?:,|и|and|&)\\s*(?:"_s
        + kWeekdayAny + u"))+)"_s + kEnd);
    static const QRegularExpression dayPlural = rx(
        u"\\b(?:по\\s+(понедельникам|вторникам|средам|четвергам|пятницам|субботам|воскресеньям)|"
        u"(?:every|on)\\s+(mondays|tuesdays|wednesdays|thursdays|fridays|saturdays|sundays))"_s + kEnd);
    static const QRegularExpression monthly = rx(
        u"\\b(?:(?:каждое|каждого)\\s+(\\d{1,2})(?:-?(?:е|го))?\\s+числ[оа](?:\\s+каждого\\s+месяца)?|"
        u"(\\d{1,2})(?:-?(?:е|го))?\\s+числа\\s+каждого\\s+месяца|"
        u"every\\s+(\\d{1,2})(?:st|nd|rd|th)?\\s+(?:of\\s+(?:the|each|every)\\s+month|day\\s+of\\s+the\\s+month)|"
        u"каждый\\s+месяц|ежемесячно|every\\s+month|monthly)"_s + kEnd);
    if (const auto m = w.take(interval); m.hasMatch()) {
        const QString unit = m.captured(3).toLower();
        qint64 n = m.captured(1).isEmpty() ? (m.captured(2).isEmpty() ? 1 : 2) : m.captured(1).toLongLong();
        QString code;
        if (unit == u"полчаса") { n = 30; code = u"m"_s; }
        else if (unit.startsWith(u"мин") || unit.startsWith(u"min")) code = u"m"_s;
        else if (unit.startsWith(u"час") || unit.startsWith(u"hour")) code = u"h"_s;
        else code = u"d"_s;
        result.repeat = code == u"d" && n == 1 ? u"daily"_s : u"every:%1%2"_s.arg(n).arg(code);
        if (!validRepeat(result.repeat))
            return result;
    } else if (w.take(rx(u"\\b(?:каждый\\s+день|ежедневно|every\\s+day|each\\s+day|daily)\\b"_s)).hasMatch()) {
        result.repeat = u"daily"_s;
    } else if (w.take(rx(u"\\b(?:каждое\\s+утро|every\\s+morning)\\b"_s)).hasMatch()) {
        result.repeat = u"daily"_s;
        partHour = options.morning;
    } else if (w.take(rx(u"\\b(?:каждый\\s+вечер|every\\s+evening|every\\s+night)\\b"_s)).hasMatch()) {
        result.repeat = u"daily"_s;
        partHour = options.evening;
    } else if (w.take(rx(u"\\b(?:по\\s+будням|в\\s+будни|каждый\\s+будний\\s+день|on\\s+weekdays|every\\s+weekday|weekdays)\\b"_s)).hasMatch()) {
        result.repeat = u"weekdays"_s;
    } else if (w.take(rx(u"\\b(?:по\\s+выходным|в\\s+выходные|каждые\\s+выходные|on\\s+weekends|every\\s+weekend|weekends)\\b"_s)).hasMatch()) {
        result.repeat = u"weekends"_s;
    } else if (const auto m = w.take(daySet); m.hasMatch()) {
        QList<int> days;
        for (const QString &part : m.captured(1).split(rx(u"\\s*(?:,|\\bи\\b|\\band\\b|&)\\s*"_s), Qt::SkipEmptyParts))
            if (const int d = weekdayAny(part.trimmed()); d > 0 && !days.contains(d))
                days.append(d);
        std::sort(days.begin(), days.end());
        QStringList list;
        for (int d : days)
            list.append(QString::number(d));
        result.repeat = days.size() == 1 ? u"weekly"_s : u"days:"_s + list.join(u',');
        if (days.size() == 1)
            weekday = days.first();
    } else if (const auto m = w.take(dayPlural); m.hasMatch()) {
        result.repeat = u"weekly"_s;
        weekday = weekdayAny(m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
    } else if (const auto m = w.take(rx(u"\\b(?:каждый|каждую|каждое|every)\\s+("_s + kWeekdays + u")\\b"_s)); m.hasMatch()) {
        result.repeat = u"weekly"_s;
        weekday = weekdayIndex(m.captured(1));
    } else if (w.take(rx(u"\\b(?:каждую\\s+неделю|еженедельно|every\\s+week|weekly)\\b"_s)).hasMatch()) {
        result.repeat = u"weekly"_s;
    } else if (const auto m = w.take(monthly); m.hasMatch()) {
        result.repeat = u"monthly"_s;
        for (int g = 1; g <= 3; ++g)
            if (!m.captured(g).isEmpty())
                monthDay = m.captured(g).toInt();
        if (monthDay > 31)
            return result;
    } else if (w.take(rx(u"\\b(?:каждый\\s+год|ежегодно|every\\s+year|yearly|annually)\\b"_s)).hasMatch()) {
        result.repeat = u"yearly"_s;
    }

    // ---- Lead time: "за 15 минут до встречи в 10" ----
    qint64 lead = 0;
    static const QRegularExpression leadTime = rx(
        u"\\bза\\s+(?:(\\d{1,3})\\s*)?(полчаса|минут[уы]?|мин\\.?|час(?:а|ов)?|дн(?:я|ей)|день)\\s+до\\b|"
        u"\\b(\\d{1,3}|an?|half\\s+an)\\s+(minutes?|mins?|hours?|days?)\\s+(?:before|early)\\b"_s);
    if (const auto m = w.take(leadTime); m.hasMatch()) {
        const bool ruLead = !m.captured(2).isEmpty();
        const QString number = ruLead ? m.captured(1) : m.captured(3);
        const QString unit = (ruLead ? m.captured(2) : m.captured(4)).toLower();
        double n = number.isEmpty() || number.startsWith(u'a') ? 1 : number.toDouble();
        if (number.startsWith(u"half"))
            n = 0.5;
        if (unit == u"полчаса")
            lead = 1800;
        else if (unit.startsWith(u"мин") || unit.startsWith(u"min"))
            lead = qint64(n * 60);
        else if (unit.startsWith(u"час") || unit.startsWith(u"hour"))
            lead = qint64(n * 3600);
        else
            lead = qint64(n * 86400);
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
        partHour = p.contains(u"утр") || p.contains(u"morning") ? options.morning
                   : p.startsWith(u"дн") || p.contains(u"afternoon") ? options.afternoon
                   : p.contains(u"веч") || p.contains(u"evening") ? options.evening : 22;
    }
    if (tonight && partHour < 0)
        partHour = 20;

    const QString rest = w.original.simplified().remove(rx(u"^[\\s,:;—–-]+"_s));
    result.text = cleanText(w.original);
    const bool timed = delta >= 0 || hour >= 0 || dayOffset >= 0 || weekday > 0 || explicitDate.isValid()
                       || partHour >= 0 || !result.repeat.isEmpty();
    // Interval repetitions without a time start one interval from now.
    if (const qint64 step = intervalSeconds(result.repeat); step > 0 && hour < 0 && dayOffset < 0 && !explicitDate.isValid()) {
        result.due = now.addSecs(step);
        result.kind = ParsedReminder::Reminder;
        return result;
    }
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

    if (monthDay > 0 && !explicitDate.isValid()) {
        QDate d(now.date().year(), now.date().month(), 1);
        explicitDate = d.addDays(std::min(monthDay, d.daysInMonth()) - 1);
        if (explicitDate < now.date()) {
            d = d.addMonths(1);
            explicitDate = d.addDays(std::min(monthDay, d.daysInMonth()) - 1);
        }
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
        time = now.time() < QTime(options.evening - 1, 0) ? QTime(options.evening, 0) : QTime(now.time().hour(), 0).addSecs(2 * 3600);
    else
        time = QTime(options.morning, 0);
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
        } else if (result.repeat == u"monthly")
            due = due.addMonths(1);
        else if (result.repeat == u"yearly")
            due = due.addYears(1);
        else
            due = due.addDays(1);
    }
    // Weekdays, weekends, chosen days: the first day that fits.
    for (int guard = 0; guard < 8 && !firesOn(result.repeat, due.date()); ++guard)
        due = due.addDays(1);
    if (lead > 0 && due.addSecs(-lead) > now)
        due = due.addSecs(-lead);
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
    if (repeat.isEmpty() || !due.isValid() || !validRepeat(repeat))
        return {};
    if (const qint64 step = intervalSeconds(repeat); step > 0) {
        if (due > after)
            return due;
        return due.addSecs((due.secsTo(after) / step + 1) * step);
    }
    QDateTime next = due;
    if (repeat == u"monthly" || repeat == u"yearly") {
        // Keep the day of the month: the 31st falls back to the last day of shorter months.
        const int day = due.date().day();
        for (int k = 1; k < 2400 && next <= after; ++k) {
            const QDate base = repeat == u"monthly" ? QDate(due.date().year(), due.date().month(), 1).addMonths(k)
                                                    : QDate(due.date().year() + k, due.date().month(), 1);
            next = QDateTime(base.addDays(std::min(day, base.daysInMonth()) - 1), due.time());
        }
        return next;
    }
    const int step = repeat == u"weekly" ? 7 : 1;
    // Jump close to `after` first, so a long-offline daemon does not loop for years.
    const qint64 behind = next.date().daysTo(after.date());
    if (behind > 7)
        next = next.addDays((behind / step - 1) * step);
    for (int guard = 0; guard < 40 && (next <= after || !firesOn(repeat, next.date())); ++guard)
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
    if (repeat == u"weekends")
        return ru ? u"по выходным"_s : u"on weekends"_s;
    if (repeat == u"monthly")
        return (ru ? u"каждый месяц, %1-го"_s : u"monthly, on day %1"_s).arg(due.isValid() ? due.date().day() : 1);
    if (repeat == u"yearly")
        return (ru ? u"каждый год, %1"_s : u"every year, %1"_s)
            .arg(QLocale(ru ? QLocale::Russian : QLocale::English).toString(due.date(), u"d MMMM"_s));
    if (repeat.startsWith(u"days:")) {
        static const QStringList ruShort = {u"пн"_s, u"вт"_s, u"ср"_s, u"чт"_s, u"пт"_s, u"сб"_s, u"вс"_s};
        QStringList names;
        for (const QString &d : repeat.mid(5).split(u','))
            names.append(ru ? ruShort.value(d.toInt() - 1) : QLocale(QLocale::English).dayName(d.toInt(), QLocale::ShortFormat));
        return (ru ? u"по "_s : u"on "_s) + names.join(u", "_s);
    }
    if (const qint64 step = intervalSeconds(repeat); step > 0) {
        const QString n = repeat.mid(6).chopped(1);
        const QChar unit = repeat.back();
        if (ru)
            return u"каждые %1 %2"_s.arg(n, unit == u'm' ? u"мин"_s : unit == u'h' ? u"ч"_s : u"дн."_s);
        return u"every %1 %2"_s.arg(n, unit == u'm' ? u"min"_s : unit == u'h' ? u"h"_s : u"days"_s);
    }
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
