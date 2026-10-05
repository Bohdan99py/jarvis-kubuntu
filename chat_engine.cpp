#include "chat_engine.h"

#include <QDate>
#include <QList>
#include <QLocale>
#include <QRandomGenerator>
#include <QStringList>
#include <QTime>
#include <QTimer>

#include <utility>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

bool hasCyrillic(const QString &s)
{
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0x0400 && u <= 0x04FF)
            return true;
    }
    return false;
}

// Lowercase, ё->е, drop apostrophes, collapse everything that is not a
// letter/digit into single spaces, and pad with spaces on both sides so that
// "contains(" key ")" gives cheap whole-word / whole-phrase matching.
QString normalize(const QString &in)
{
    QString out;
    out.reserve(in.size() + 2);
    out += QLatin1Char(' ');
    bool lastSpace = true;

    const QString lower = in.toLower();
    for (QChar c : lower) {
        if (c == u'ё')
            c = u'е';
        if (c == u'\'' || c == u'’')
            continue;
        if (c.isLetterOrNumber()) {
            out += c;
            lastSpace = false;
        } else if (!lastSpace) {
            out += QLatin1Char(' ');
            lastSpace = true;
        }
    }
    if (!lastSpace)
        out += QLatin1Char(' ');
    return out;
}

using SysHandler = QString (SystemInfo::*)(bool) const;

struct Rule
{
    QStringList keys;      // normalized, space-padded
    QStringList notKeys;   // if any of these is present, the rule is skipped
    QStringList ru;        // canned replies (unused when handler is set)
    QStringList en;
    bool needsFollowUp;    // short reply; add "how can I help" if it stands alone
    SysHandler handler;    // live system report instead of a canned reply
};

QStringList normalizeAll(const QStringList &list)
{
    QStringList out;
    out.reserve(list.size());
    for (const QString &k : list)
        out.append(normalize(k));
    return out;
}

Rule textRule(const QStringList &keys, QStringList ru, QStringList en,
              bool needsFollowUp = false)
{
    return Rule{normalizeAll(keys), {}, std::move(ru), std::move(en), needsFollowUp, nullptr};
}

Rule sysRule(const QStringList &keys, SysHandler handler, const QStringList &notKeys = {})
{
    return Rule{normalizeAll(keys), normalizeAll(notKeys), {}, {}, false, handler};
}

bool matches(const QString &padded, const QStringList &keys)
{
    for (const QString &key : keys) {
        if (padded.contains(key))
            return true;
    }
    return false;
}

// Order matters: replies of all matching rules are joined in this order,
// so "привет, как дела" -> greeting + how-are-you.
const QList<Rule> &rules()
{
    static const QList<Rule> table = {
        textRule(
            {u"привет"_s, u"здравствуй"_s, u"здравствуйте"_s, u"добрый день"_s,
             u"добрый вечер"_s, u"доброе утро"_s, u"хай"_s, u"hello"_s, u"hi"_s,
             u"hey"_s, u"good morning"_s, u"good evening"_s},
            {u"Привет!"_s, u"Здравствуй!"_s, u"Приветствую!"_s, u"Рад тебя слышать!"_s},
            {u"Hello!"_s, u"Hi there!"_s, u"Greetings!"_s},
            /*needsFollowUp=*/true),

        textRule(
            {u"как дела"_s, u"как ты"_s, u"как жизнь"_s, u"как настроение"_s,
             u"как поживаешь"_s, u"как сам"_s, u"how are you"_s, u"how are u"_s,
             u"how is it going"_s, u"hows it going"_s, u"whats up"_s},
            {u"Всё отлично: системы в норме, процессор не перегревается. А у тебя как дела?"_s,
             u"Работаю в штатном режиме, багов пока не замечено. Как ты сам?"_s,
             u"Лучше всех в этом терминале! А у тебя как дела?"_s},
            {u"All good — systems nominal. How about you?"_s,
             u"Running smoothly, no bugs spotted yet. How are you?"_s}),

        textRule(
            {u"как тебя зовут"_s, u"кто ты"_s, u"твое имя"_s, u"who are you"_s,
             u"your name"_s, u"what are you"_s},
            {u"Я J.A.R.V.I.S. — Just A Rather Very Intelligent System. Пока ещё совсем молодой, но растущий."_s},
            {u"I'm J.A.R.V.I.S. — Just A Rather Very Intelligent System. Still young, but growing."_s}),

        textRule(
            {u"что ты умеешь"_s, u"что умеешь"_s, u"помощь"_s, u"помоги"_s,
             u"help"_s, u"what can you do"_s},
            {u"Умею болтать и смотреть на состояние компьютера. Спроси: «как дела», "
             u"«загрузка процессора», «память», «батарея», «температура», «диск», "
             u"«аптайм», «информация о системе», «топ процессов», «который час», «какая дата»."_s},
            {u"I can chat and check on the computer. Ask: \"how are you\", \"cpu usage\", "
             u"\"memory\", \"battery\", \"temperature\", \"disk space\", \"uptime\", "
             u"\"system info\", \"top processes\", \"what time is it\", \"what date is it\"."_s}),

        // ---- live system reports (C layer) ----
        sysRule({u"топ процессов"_s, u"процессы"_s, u"самые тяжелые процессы"_s,
                 u"что ест память"_s, u"что жрет память"_s, u"кто ест память"_s,
                 u"кто жрет память"_s, u"top processes"_s, u"processes"_s,
                 u"what is eating memory"_s, u"whats eating memory"_s},
                &SystemInfo::processesReport),

        sysRule({u"процессор"_s, u"загрузка процессора"_s, u"загрузка цп"_s,
                 u"нагрузка"_s, u"cpu"_s, u"processor"_s},
                &SystemInfo::cpuReport),

        sysRule({u"память"_s, u"оперативка"_s, u"оперативная память"_s, u"озу"_s,
                 u"ram"_s, u"memory"_s},
                &SystemInfo::memoryReport,
                {u"ест память"_s, u"жрет память"_s, u"eating memory"_s}),

        sysRule({u"батарея"_s, u"батарейка"_s, u"заряд"_s, u"аккумулятор"_s,
                 u"battery"_s},
                &SystemInfo::batteryReport),

        sysRule({u"температура"_s, u"температуру"_s, u"градусов"_s,
                 u"temperature"_s, u"how hot"_s},
                &SystemInfo::temperatureReport),

        sysRule({u"аптайм"_s, u"uptime"_s, u"как долго работает"_s,
                 u"сколько работает компьютер"_s, u"how long running"_s},
                &SystemInfo::uptimeReport),

        sysRule({u"диск"_s, u"место на диске"_s, u"свободное место"_s,
                 u"disk"_s, u"disk space"_s, u"free space"_s, u"storage"_s},
                &SystemInfo::diskReport),

        sysRule({u"информация о системе"_s, u"о системе"_s, u"что за система"_s,
                 u"какая система"_s, u"версия системы"_s, u"system info"_s,
                 u"sysinfo"_s, u"uname"_s},
                &SystemInfo::systemReport),

        // ---- time and date ----
        textRule(
            {u"который час"_s, u"сколько времени"_s, u"сколько сейчас времени"_s,
             u"what time"_s, u"current time"_s},
            {u"Сейчас %time%."_s},
            {u"It's %time%."_s}),

        textRule(
            {u"какое число"_s, u"какая дата"_s, u"какой сегодня день"_s,
             u"какое сегодня число"_s, u"what date"_s, u"what day"_s, u"todays date"_s},
            {u"Сегодня %date%."_s},
            {u"Today is %date%."_s}),

        textRule(
            {u"спасибо"_s, u"благодарю"_s, u"thanks"_s, u"thank you"_s},
            {u"Всегда пожалуйста!"_s, u"Обращайся!"_s},
            {u"You're welcome!"_s, u"Anytime!"_s}),

        textRule(
            {u"пока"_s, u"до свидания"_s, u"до встречи"_s, u"bye"_s, u"goodbye"_s,
             u"see you"_s},
            {u"До встречи! Я буду здесь."_s},
            {u"See you! I'll be right here."_s}),
    };
    return table;
}

const QStringList &fallbackRu()
{
    static const QStringList v = {
        u"Этого я пока не умею. Попробуй «привет», «как дела», «память», «загрузка процессора» "
        u"или «топ процессов»."_s,
        u"Пока не знаю, как на это ответить, но это в планах. Напиши «помощь», чтобы увидеть, "
        u"что я умею."_s,
    };
    return v;
}

const QStringList &fallbackEn()
{
    static const QStringList v = {
        u"I can't do that yet. Try \"hello\", \"how are you\", \"memory\", \"cpu usage\" "
        u"or \"top processes\"."_s,
        u"Not sure how to answer that yet, but it's on the roadmap. Type \"help\" to see "
        u"what I can do."_s,
    };
    return v;
}

const QString &pick(const QStringList &list)
{
    return list.at(int(QRandomGenerator::global()->bounded(quint32(list.size()))));
}

} // namespace

ChatEngine::ChatEngine(QObject *parent)
    : QObject(parent)
    , m_sys(this)
{
}

QString ChatEngine::respond(const QString &input) const
{
    const bool ru = hasCyrillic(input);
    const QString padded = normalize(input);

    QStringList parts;
    const Rule *only = nullptr;
    bool anyReport = false;

    for (const Rule &rule : rules()) {
        if (!matches(padded, rule.keys))
            continue;
        if (!rule.notKeys.isEmpty() && matches(padded, rule.notKeys))
            continue;

        parts.append(rule.handler ? (m_sys.*rule.handler)(ru)
                                  : pick(ru ? rule.ru : rule.en));
        anyReport = anyReport || rule.handler != nullptr;
        only = &rule;
    }

    if (parts.isEmpty())
        return pick(ru ? fallbackRu() : fallbackEn());

    if (parts.size() == 1 && only->needsFollowUp)
        parts.append(ru ? u"Чем могу помочь?"_s : u"How can I help?"_s);

    // Small talk reads as one paragraph; system reports each get their own line.
    QString reply = parts.join(anyReport ? QChar(u'\n') : QChar(u' '));

    if (reply.contains(u"%time%"_s) || reply.contains(u"%date%"_s)) {
        const QLocale loc(ru ? QLocale::Russian : QLocale::English);
        reply.replace(u"%time%"_s, loc.toString(QTime::currentTime(), u"HH:mm"_s));
        reply.replace(u"%date%"_s, loc.toString(QDate::currentDate(), QLocale::LongFormat));
    }
    return reply;
}

quint64 ChatEngine::ask(const QString &input)
{
    const quint64 id = ++m_nextId;
    QString reply = respond(input);
    const int delayMs = 300 + int(QRandomGenerator::global()->bounded(500u));
    QTimer::singleShot(delayMs, this, [this, id, reply = std::move(reply)] {
        emit replyReady(id, reply);
    });
    return id;
}

} // namespace jarvis
