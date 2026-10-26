#include "agent_intents.h"

#include <QHash>
#include <QSet>
#include <QRegularExpression>

#include "workspace.h"

using namespace Qt::StringLiterals;

namespace jarvis::agent {
namespace {

// " text " padded with spaces, so " stem" matches a word start.
struct Text
{
    QString t;
    bool has(const char16_t *stem) const { return t.contains(u' ' + QString::fromUtf16(stem)); }
    bool any(std::initializer_list<const char16_t *> stems) const
    {
        for (const char16_t *s : stems)
            if (has(s))
                return true;
        return false;
    }
    // A whole word or phrase.
    bool word(const char16_t *w) const { return t.contains(u' ' + QString::fromUtf16(w) + u' '); }
    bool words(std::initializer_list<const char16_t *> ws) const
    {
        for (const char16_t *w : ws)
            if (word(w))
                return true;
        return false;
    }
};

Step step(const QString &tool, const QJsonObject &args = {}) { return Step{tool, args}; }

QRegularExpression rx(const QString &p)
{
    return QRegularExpression(p, QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption);
}

const QHash<QString, int> &numberWords()
{
    static const QHash<QString, int> words{
        {u"ноль"_s, 0},       {u"нуля"_s, 0},        {u"один"_s, 1},        {u"одна"_s, 1},         {u"одного"_s, 1},
        {u"два"_s, 2},        {u"две"_s, 2},         {u"двух"_s, 2},        {u"три"_s, 3},          {u"трех"_s, 3},
        {u"четыре"_s, 4},     {u"пять"_s, 5},        {u"пяти"_s, 5},        {u"шесть"_s, 6},        {u"семь"_s, 7},
        {u"восемь"_s, 8},     {u"девять"_s, 9},      {u"десять"_s, 10},     {u"десяти"_s, 10},      {u"одиннадцать"_s, 11},
        {u"двенадцать"_s, 12}, {u"тринадцать"_s, 13}, {u"четырнадцать"_s, 14}, {u"пятнадцать"_s, 15}, {u"пятнадцати"_s, 15},
        {u"шестнадцать"_s, 16}, {u"семнадцать"_s, 17}, {u"восемнадцать"_s, 18}, {u"девятнадцать"_s, 19},
        {u"двадцать"_s, 20},  {u"двадцати"_s, 20},   {u"тридцать"_s, 30},   {u"тридцати"_s, 30},    {u"сорок"_s, 40},
        {u"сорока"_s, 40},    {u"пятьдесят"_s, 50},  {u"пятидесяти"_s, 50}, {u"шестьдесят"_s, 60},  {u"шестидесяти"_s, 60},
        {u"семьдесят"_s, 70}, {u"семидесяти"_s, 70}, {u"восемьдесят"_s, 80}, {u"восьмидесяти"_s, 80}, {u"девяносто"_s, 90},
        {u"девяноста"_s, 90}, {u"сто"_s, 100},       {u"ста"_s, 100},
        {u"zero"_s, 0},       {u"one"_s, 1},         {u"two"_s, 2},         {u"three"_s, 3},        {u"four"_s, 4},
        {u"five"_s, 5},       {u"six"_s, 6},         {u"seven"_s, 7},       {u"eight"_s, 8},        {u"nine"_s, 9},
        {u"ten"_s, 10},       {u"eleven"_s, 11},     {u"twelve"_s, 12},     {u"thirteen"_s, 13},    {u"fourteen"_s, 14},
        {u"fifteen"_s, 15},   {u"sixteen"_s, 16},    {u"seventeen"_s, 17},  {u"eighteen"_s, 18},    {u"nineteen"_s, 19},
        {u"twenty"_s, 20},    {u"thirty"_s, 30},     {u"forty"_s, 40},      {u"fifty"_s, 50},       {u"sixty"_s, 60},
        {u"seventy"_s, 70},   {u"eighty"_s, 80},     {u"ninety"_s, 90},     {u"hundred"_s, 100},
    };
    return words;
}

// After a verb and a few filler words: what the user named ("открой мне, пожалуйста, телеграм" → "телеграм").
QString objectAfter(const QString &clause, const QStringList &verbs)
{
    QString t = clause;
    for (const QString &v : verbs) {
        const QRegularExpression r = rx(u"(?:^|\\s)"_s + v + u"\\S*\\s+(.*)$"_s);
        const auto m = r.match(t);
        if (m.hasMatch()) {
            t = m.captured(1);
            break;
        }
    }
    static const QRegularExpression filler = rx(
        u"^(?:(?:мне|пожалуйста|please|приложение|программу|программа|окно|app|application|the|a|my|мой|мою|моё|мое)\\s+)+"_s);
    t.remove(filler);
    static const QRegularExpression tail = rx(u"\\s+(?:пожалуйста|please)$"_s);
    t.remove(tail);
    return t.trimmed();
}

// The name of the headphones after the word: «наушники Sony WH» → "sony wh".
QString headphonesName(const QString &clause)
{
    static const QRegularExpression r = rx(
        u"(?:наушник\\S*|гарнитур\\S*|headphones?|headset|earbuds?)\\s+(?:(?:мои|моих|мой|my|the|под названием|называются|called)\\s+)*(.+)$"_s);
    const auto m = r.match(clause);
    if (!m.hasMatch())
        return {};
    QString name = m.captured(1).trimmed();
    static const QRegularExpression stop = rx(u"^(?:и|а|на|в|по|через|пожалуйста|please|and|then)\\b.*$"_s);
    if (stop.match(name).hasMatch())
        return {};
    return name.left(60);
}

// "максимум", "половина", "четверть": a level by itself, never a change.
int levelWord(const Text &x)
{
    if (x.any({u"полную", u"максимум", u"максимальн", u"max", u"full"}))
        return 100;
    if (x.any({u"половин", u"наполовину", u"half"}))
        return 50;
    if (x.any({u"четверть", u"quarter"}))
        return 25;
    if (x.any({u"минимум", u"минимальн", u"min "}))
        return 5;
    return -1;
}

int volumeNumber(const Text &x, const QString &clause)
{
    const int w = levelWord(x);
    return w >= 0 ? w : parseNumber(clause);
}

int relativeStep(const Text &x, int base)
{
    if (x.any({u"намного", u"сильно", u"гораздо", u"much ", u"a lot"}))
        return base * 2;
    if (x.any({u"чуть", u"немного", u"слегка", u"капельку", u"a bit", u"slightly", u"little"}))
        return base / 2;
    return base;
}

// The level or the change a clause asks for, `dir` -1 quieter/darker, +1
// louder/brighter, 0 none: "громкость 20" → level 20, "тише на 10" → change -10,
// "убавь до 20" → level 20, "чуть громче" → change +base/2. False: no amount.
bool amount(const Text &x, const QString &clause, int dir, int base, QJsonObject *args)
{
    const int word = levelWord(x);
    const int n = word >= 0 ? word : parseNumber(clause);
    if (dir == 0) {
        if (n < 0)
            return false;
        *args = {{u"level"_s, n}};
        return true;
    }
    if (n < 0) {
        *args = {{u"change"_s, dir * relativeStep(x, base)}};
        return true;
    }
    // "до 20", "to 20": where to end up; "на 20", "by 20", "+20": how much to move.
    static const QRegularExpression to = rx(u"(?:^|\\s)(?:до|to|at)\\s+(\\S+)"_s);
    const auto m = to.match(clause);
    if (word >= 0 || (m.hasMatch() && parseNumber(m.captured(1)) >= 0))
        *args = {{u"level"_s, n}};
    else
        *args = {{u"change"_s, dir * std::min(n, 100)}};
    return true;
}

bool negated(const Text &x)
{
    // "не беспокоить" is a name; "не надо", "не включай" are not requests to act.
    QString t = x.t;
    t.replace(u" не беспокоить"_s, u" беспокоить"_s);
    t.replace(u" не отвлекали"_s, u" отвлекали"_s);
    t.replace(u" не слышно"_s, u" слышно"_s);
    t.replace(u" do not disturb"_s, u" disturb"_s);
    t.replace(u" don t disturb"_s, u" disturb"_s);
    // normalizeCommand turns "don't" into "don t".
    return t.contains(u" не "_s) || t.contains(u" don t "_s) || t.contains(u" do not "_s) || t.contains(u" dont "_s)
           || t.contains(u" never "_s) || t.contains(u" нельзя "_s);
}

const QStringList kOpenVerbs{u"открой"_s, u"открыть"_s, u"запусти"_s, u"запустить"_s, u"включи"_s, u"open"_s, u"launch"_s, u"start"_s, u"run"_s};
const QStringList kFocusVerbs{u"переключись на"_s, u"переключись в"_s, u"перейди в"_s, u"перейди на"_s, u"покажи окно"_s, u"switch to"_s,
                              u"focus"_s};
const QStringList kMaximizeVerbs{u"разверни"_s, u"развернуть"_s, u"раскрой"_s, u"maximize"_s, u"maximise"_s};
const QStringList kCloseVerbs{u"закрой"_s, u"закрыть"_s, u"выйди из"_s, u"close"_s, u"quit"_s, u"exit"_s};

enum class Clause { Unknown, Parsed, Declined };

// Nothing but a verb, a pronoun and an amount: "убавь немного", "turn it down a bit", "прибавь на 10".
bool onlyAmountWords(const QString &clause)
{
    static const QSet<QString> filler{
        u"сделай"_s, u"сделать"_s, u"убавь"_s, u"прибавь"_s, u"уменьши"_s, u"увеличь"_s, u"снизь"_s, u"понизь"_s, u"повысь"_s, u"подними"_s,
        u"ещё"_s, u"еще"_s, u"чуть"_s, u"немного"_s, u"немножко"_s, u"слегка"_s, u"капельку"_s, u"намного"_s, u"сильно"_s, u"гораздо"_s,
        u"его"_s, u"это"_s, u"на"_s, u"до"_s, u"процентов"_s, u"процента"_s, u"процент"_s, u"пожалуйста"_s, u"turn"_s, u"make"_s,
        u"bring"_s, u"put"_s, u"it"_s, u"this"_s, u"that"_s, u"up"_s, u"down"_s, u"a"_s, u"bit"_s, u"little"_s, u"lot"_s, u"by"_s,
        u"to"_s, u"percent"_s, u"please"_s, u"lower"_s, u"raise"_s, u"decrease"_s, u"increase"_s, u"reduce"_s, u"more"_s, u"%"_s};
    for (const QString &w : clause.split(u' ', Qt::SkipEmptyParts)) {
        QString word = w;
        word.remove(u'%');
        if (word.isEmpty() || filler.contains(word) || numberWords().contains(word) || word.toInt() > 0 || word == u"0")
            continue;
        return false;
    }
    return true;
}

// One clause → its steps: Parsed, Unknown when the rules do not understand it,
// Declined when it says not to do something.
Clause parseClause(const QString &clause, Parsed *p, QString *lastVerb)
{
    QList<Step> *out = &p->steps;
    bool *systemish = &p->systemish;
    const Text x{u' ' + clause + u' '};
    const bool domain = x.any({u"звук", u"громк", u"тише", u"громче", u"наушник", u"гарнитур", u"динамик", u"колонк", u"блютуз", u"блютус",
                               u"bluetooth", u"яркост", u"ярче", u"темне", u"экран", u"микрофон", u"батаре", u"заряд", u"аккумулятор",
                               u"питани", u"энерг", u"интернет", u"сеть", u"wifi", u"wi-fi", u"уведомлен", u"беспокоить", u"отвлека",
                               u"приложени", u"окно", u"открой", u"запусти", u"закрой", u"включи", u"выключи", u"подключи", u"отключи",
                               u"переключи", u"выведи", u"верни", u"убавь", u"прибавь", u"sound", u"volume", u"audio", u"headphone",
                               u"headset", u"speaker", u"brightness", u"screen", u"microphone", u"battery", u"power", u"internet",
                               u"network", u"notification", u"disturb", u"open", u"launch", u"close", u"turn", u"switch", u"connect",
                               u"mute", u"louder", u"quieter", u"пауз", u"музык", u"трек", u"pause", u"track"});
    *systemish = *systemish || domain;
    // "почему не работает звук", "мне ничего не слышно": a diagnosis, not a negation.
    if (x.any({u"почему", u"why", u"не работает", u"не работают", u"не слышно", u"не включается", u"не подключается",
               u"doesn't work", u"does not work", u"not working", u"can't hear", u"диагностик", u"diagnos"})) {
        QString what;
        if (x.any({u"блютуз", u"блютус", u"bluetooth"}))
            what = u"bluetooth"_s;
        else if (x.any({u"звук", u"слышно", u"наушник", u"динамик", u"микрофон", u"sound", u"audio", u"hear", u"headphone", u"microphone"}))
            what = u"audio"_s;
        else if (x.any({u"яркост", u"экран", u"brightness", u"screen"}))
            what = u"brightness"_s;
        else if (x.any({u"интернет", u"сеть", u"сети", u"wifi", u"wi-fi", u"internet", u"network"}))
            what = u"network"_s;
        else if (x.any({u"окн", u"приложени", u"window", u"app"}))
            what = u"apps"_s;
        else if (x.any({u"модел", u"нейросет", u"model"}))
            what = u"model"_s;
        else if (x.any({u"голос", u"озвуч", u"распозна", u"диктовк", u"voice", u"speech", u"dictation"}))
            what = u"voice"_s;
        if (!what.isEmpty()) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, what}}));
            return Clause::Parsed;
        }
    }
    // "не меняй громкость": heard, and nothing is done about it. "I don't like jazz"
    // is not about the laptop: the chat hears it.
    if (negated(x))
        return domain ? Clause::Declined : Clause::Unknown;
    // "как включить блютуз?" asks how, it does not ask to do it.
    // (Whole-message questions — "как поставить…", "how do I…" — are told apart in parse().)
    static const QRegularExpression question = rx(
        u"^(?:как (?:\\S+ть|\\S+ти)\\b|каким образом|зачем|что такое|что значит|где|можно ли|how (?:do|can|to)\\b|what does|where|can i|is it possible)\\b"_s);
    if (question.match(clause).hasMatch() && !x.any({u"почему", u"why"}))
        return Clause::Unknown;

    const bool soundWord = x.any({u"звук", u"аудио", u"музык", u"вывод", u"выход", u"sound", u"audio", u"music", u"output"});
    const bool headphones = x.any({u"наушник", u"гарнитур", u"airpods", u"headphone", u"headset", u"earbud", u"блютуз-уш"})
                            || x.words({u"уши", u"ушах", u"ушки", u"ушках", u"ушами"});
    const bool speakers = x.any({u"динамик", u"ноутбук", u"ноут ", u"встроенн", u"колонки ноутбука", u"speaker", u"laptop", u"built-in"});
    const bool mic = x.any({u"микрофон", u"microphone", u"mic "});
    const bool bt = x.any({u"блютуз", u"блютус", u"bluetooth"});
    const bool offVerb = x.any({u"выключи", u"отключи", u"выруби", u"turn off", u"switch off", u"disable", u"disconnect"});
    const bool onVerb = x.any({u"включи", u"вруби", u"turn on", u"switch on", u"enable"});
    // "turn the sound off", "turn Bluetooth on".
    const bool offWord = offVerb || (x.any({u"turn", u"switch", u"put"}) && x.word(u"off"));
    const bool onWord = onVerb || (x.any({u"turn", u"switch"}) && x.word(u"on"));

    // ---- Microphone ----
    if (mic) {
        if (x.any({u"какие", u"список", u"покажи", u"доступн", u"which", u"list", u"show"})) {
            out->append(step(u"audio.list"_s));
            return Clause::Parsed;
        }
        if (x.any({u"встроенн", u"ноутбук", u"builtin", u"built-in", u"internal"})) {
            out->append(step(u"audio.input"_s, {{u"target"_s, u"builtin"_s}}));
            return Clause::Parsed;
        }
        if (x.any({u"гарнитур", u"наушник", u"headset", u"headphone", u"bluetooth", u"блютуз"})) {
            out->append(step(u"audio.input"_s, {{u"target"_s, u"headset"_s}}));
            return Clause::Parsed;
        }
        static const QRegularExpression named = rx(u"микрофон\\S*\\s+(?:на\\s+)?(.+)$|microphone\\s+(?:to\\s+)?(.+)$"_s);
        const auto m = named.match(clause);
        if (m.hasMatch() && x.any({u"выбери", u"переключи", u"поставь", u"choose", u"switch", u"use"})) {
            out->append(step(u"audio.input"_s, {{u"name"_s, (m.captured(1) + m.captured(2)).trimmed().left(60)}}));
            return Clause::Parsed;
        }
        out->append(step(u"audio.list"_s));
        return Clause::Parsed;
    }

    // ---- Headphones and speakers ----
    if (headphones) {
        if (x.any({u"нов", u"сопряг", u"сопряж", u"pair", u"new "})) {
            out->append(step(u"bluetooth.pair"_s));
            return Clause::Parsed;
        }
        if (offVerb && !soundWord) {
            out->append(step(u"bluetooth.disconnect"_s, {{u"name"_s, headphonesName(clause)}}));
            return Clause::Parsed;
        }
        if (x.any({u"почему", u"не слышно", u"не работа", u"why"})) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, u"audio"_s}}));
            return Clause::Parsed;
        }
        QJsonObject args{{u"target"_s, u"headphones"_s}};
        if (const QString name = headphonesName(clause); !name.isEmpty())
            args[u"name"_s] = name;
        out->append(step(u"audio.output"_s, args));
        return Clause::Parsed;
    }
    if (speakers && (soundWord || x.any({u"верни", u"переключи", u"выведи", u"включи", u"switch", u"move", u"back", u"play", u"use"}))) {
        out->append(step(u"audio.output"_s, {{u"target"_s, u"speakers"_s}}));
        return Clause::Parsed;
    }

    // "еле слышно", "I can barely hear anything": too quiet, one step up.
    if (x.any({u"еле слышно", u"почти не слышно", u"плохо слышно", u"barely hear", u"hardly hear", u"can barely", u"too quiet"})) {
        out->append(step(u"audio.volume"_s, {{u"change"_s, 10}}));
        return Clause::Parsed;
    }
    // "слишком громко", "экран слишком яркий": the opposite direction, one step.
    if (x.has(u"слишком") || x.has(u"too ")) {
        if (x.any({u"громк", u"loud"})) {
            out->append(step(u"audio.volume"_s, {{u"change"_s, -10}}));
            return Clause::Parsed;
        }
        if (x.any({u"тихо", u"тихий", u"quiet"})) {
            out->append(step(u"audio.volume"_s, {{u"change"_s, 10}}));
            return Clause::Parsed;
        }
        if (x.any({u"ярк", u"светл", u"bright"})) {
            out->append(step(u"screen.brightness"_s, {{u"change"_s, -15}}));
            return Clause::Parsed;
        }
        if (x.any({u"темн", u"тускл", u"dark", u"dim"})) {
            out->append(step(u"screen.brightness"_s, {{u"change"_s, 15}}));
            return Clause::Parsed;
        }
    }

    // ---- Volume and mute ----
    // Recognition slips are common in these words ("громкасть", "грамкость", "volum").
    const bool volumeWord = x.any({u"громкост", u"громкаст", u"грамкост", u"volume", u"volum"});
    const bool soundNoun = x.any({u"звук", u"sound", u"audio", u"аудио"});
    // "turn it down", "make it quieter", "сделай потише": the sound is meant by itself.
    const int volumeDir = x.any({u"тише", u"потише", u"убавь", u"уменьши", u"снизь", u"понизь", u"убери громк", u"quieter", u"softer",
                                 u"lower", u"decrease", u"reduce"})
                                  || (x.word(u"down") && x.any({u"turn", u"volume", u"sound", u"bring", u"put"}))
                              ? -1
                          : x.any({u"громче", u"погромче", u"прибавь", u"увеличь", u"повысь", u"подними", u"louder", u"raise", u"increase",
                                   u"boost"})
                                  || (x.word(u"up") && x.any({u"turn", u"volume", u"sound", u"bring", u"put", u"pump"}))
                              ? 1
                              : 0;
    const bool impliesSound = x.any({u"тише", u"потише", u"громче", u"погромче", u"quieter", u"louder", u"softer"})
                              || (x.words({u"it", u"this", u"that"}) && x.any({u"turn", u"bring"}));
    if (x.any({u"без звука", u"замьют", u"заглуши звук", u"mute", u"silence the sound"}) && !x.has(u"unmute")) {
        out->append(step(u"audio.mute"_s, {{u"mute"_s, true}}));
        return Clause::Parsed;
    }
    if (x.has(u"unmute") || x.any({u"размьют", u"сними с беззвуч"})
        || ((onWord || x.any({u"верни звук"})) && soundNoun && !volumeWord && volumeDir == 0 && parseNumber(clause) < 0)) {
        out->append(step(u"audio.mute"_s, {{u"mute"_s, false}}));
        return Clause::Parsed;
    }
    if (offWord && soundNoun && !volumeWord && parseNumber(clause) < 0) {
        out->append(step(u"audio.mute"_s, {{u"mute"_s, true}}));
        return Clause::Parsed;
    }
    // Brightness words win over "убавь"/"lower" ("убавь яркость").
    const bool brightnessWord = x.any({u"яркост", u"brightness", u"экран", u"screen", u"display"});
    // "убавь", "прибавь немного", "turn it down a bit": a bare direction means the sound;
    // "увеличь шрифт" names something else and is not the sound.
    const bool bareVerb = volumeDir != 0 && onlyAmountWords(clause);
    // "переключи звук на 2" names no volume: a device, perhaps — not a guess at 2 %.
    const bool routeVerb = !volumeWord && x.any({u"переключи", u"выведи", u"switch", u"route", u"move"});
    if (!routeVerb && (volumeWord || ((soundNoun || impliesSound || bareVerb) && !brightnessWord))) {
        QJsonObject args;
        if (amount(x, clause, volumeDir, 10, &args)) {
            if (args.contains(u"level"_s) && args.value(u"level"_s).toInt() > 150) {
                p->problems.append(u"volume:"_s + QString::number(args.value(u"level"_s).toInt()));
                return Clause::Parsed;
            }
            if (volumeDir != 0 || volumeWord || soundNoun) {
                out->append(step(u"audio.volume"_s, args));
                return Clause::Parsed;
            }
        }
        if (volumeWord && x.any({u"какая", u"сколько", u"what", u"how"})) {
            out->append(step(u"audio.list"_s));
            return Clause::Parsed;
        }
        if (volumeWord || volumeDir != 0)
            return Clause::Unknown;
    }

    // ---- Bluetooth ----
    if (bt) {
        if (x.any({u"почему", u"не работа", u"why"})) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, u"bluetooth"_s}}));
            return Clause::Parsed;
        }
        if (x.any({u"состояни", u"статус", u"какие", u"что подключ", u"status", u"which", u"what"})) {
            out->append(step(u"bluetooth.status"_s));
            return Clause::Parsed;
        }
        if (x.any({u"сопряг", u"сопряж", u"pair"})) {
            out->append(step(u"bluetooth.pair"_s));
            return Clause::Parsed;
        }
        if (offWord) {
            out->append(step(u"bluetooth.power"_s, {{u"on"_s, false}}));
            return Clause::Parsed;
        }
        if (onWord) {
            out->append(step(u"bluetooth.power"_s, {{u"on"_s, true}}));
            return Clause::Parsed;
        }
    }
    // "подключи Sony", "connect my speaker": a known Bluetooth device by name.
    if (x.any({u"подключи", u"connect "}) && !x.any({u"интернет", u"сеть", u"wifi", u"wi-fi", u"vpn", u"internet", u"network"})) {
        const QString name = objectAfter(clause, {u"подключи"_s, u"connect"_s});
        static const QRegularExpression btWords = rx(u"^(?:к\\s+)?(?:блютуз\\S*|bluetooth|устройство|device)\\s*"_s);
        QString n = name;
        n.remove(btWords);
        if (!n.isEmpty()) {
            out->append(step(u"bluetooth.connect"_s, {{u"name"_s, n.left(60)}}));
            return Clause::Parsed;
        }
    }
    if (x.any({u"отключи", u"disconnect"}) && !x.any({u"интернет", u"сеть", u"wifi", u"уведомлен", u"звук", u"notification", u"sound"})) {
        const QString n = objectAfter(clause, {u"отключи"_s, u"disconnect"_s});
        if (!n.isEmpty()) {
            out->append(step(u"bluetooth.disconnect"_s, {{u"name"_s, n.left(60)}}));
            return Clause::Parsed;
        }
    }

    // ---- Brightness ----
    const int lightDir = x.any({u"ярче", u"светлее", u"прибавь", u"увеличь", u"повысь", u"подними", u"повыше", u"brighter", u"raise",
                                u"increase"})
                                 || x.word(u"up")
                             ? 1
                         : x.any({u"темнее", u"потемнее", u"тусклее", u"убавь", u"уменьши", u"снизь", u"понизь", u"пониже", u"darker",
                                  u"dimmer", u"dim ", u"lower", u"decrease", u"reduce"})
                                 || x.word(u"down")
                             ? -1
                             : 0;
    if (x.any({u"яркост", u"brightness"}) || x.any({u"ярче", u"светлее", u"brighter", u"темнее", u"потемнее", u"тусклее", u"darker", u"dimmer"})
        || (lightDir != 0 && x.any({u"экран", u"screen", u"display"}))) {
        if (x.any({u"почему", u"why"})) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, u"brightness"_s}}));
            return Clause::Parsed;
        }
        QJsonObject args;
        if (amount(x, clause, lightDir, 15, &args)) {
            const int level = args.value(u"level"_s).toInt(50);
            if (args.contains(u"level"_s) && (level < 1 || level > 100)) {
                p->problems.append(u"brightness:"_s + QString::number(level));
                return Clause::Parsed;
            }
            out->append(step(u"screen.brightness"_s, args));
            return Clause::Parsed;
        }
        return Clause::Unknown;
    }

    // ---- Do not disturb ----
    if (x.any({u"не беспокоить", u"do not disturb", u"dnd", u"тихий режим", u"без уведомлений", u"не отвлекали", u"не отвлекай"})) {
        out->append(step(u"notifications.dnd"_s, {{u"on"_s, !(offVerb || x.any({u"выйди", u"сними", u"убери"}))}}));
        return Clause::Parsed;
    }
    if (x.has(u"уведомлен") || x.has(u"notification")) {
        if (x.any({u"настройк", u"settings"})) {
            out->append(step(u"settings.open"_s, {{u"page"_s, u"notifications"_s}}));
            return Clause::Parsed;
        }
        if (offVerb || x.any({u"убери", u"заглуши", u"мешают", u"отвлекают", u"достали", u"silence", u"annoying", u"bother"})) {
            out->append(step(u"notifications.dnd"_s, {{u"on"_s, true}}));
            return Clause::Parsed;
        }
        if (onVerb || x.any({u"верни"})) {
            out->append(step(u"notifications.dnd"_s, {{u"on"_s, false}}));
            return Clause::Parsed;
        }
    }

    // ---- Settings pages ----
    if (x.any({u"настройк", u"параметр", u"settings"})) {
        if (x.any({u"джарвис", u"jarvis"}))
            return Clause::Unknown; // Jarvis's own settings: the window handles that
        const QString key = workspace::settingsKey(clause);
        if (!key.isEmpty()) {
            out->append(step(u"settings.open"_s, {{u"page"_s, key}}));
            return Clause::Parsed;
        }
        return Clause::Unknown;
    }

    // ---- Power, battery, network ----
    if (x.any({u"энергосбережен", u"экономи", u"power saver", u"power-saver", u"battery saver"}) && !x.any({u"почему", u"why"})) {
        out->append(step(u"power.profile"_s, {{u"profile"_s, offVerb ? u"balanced"_s : u"power-saver"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"производительн", u"performance", u"максимальн мощност"})) {
        out->append(step(u"power.profile"_s, {{u"profile"_s, offVerb ? u"balanced"_s : u"performance"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"сбалансирован", u"balanced", u"обычный режим питания"})) {
        out->append(step(u"power.profile"_s, {{u"profile"_s, u"balanced"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"батаре", u"заряд", u"аккумулятор", u"battery"})) {
        out->append(step(u"system.status"_s, {{u"what"_s, u"battery"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"профиль питания", u"режим питания", u"power profile"})) {
        out->append(step(u"system.status"_s, {{u"what"_s, u"power"_s}}));
        return Clause::Parsed;
    }
    // "есть ли интернет", not "что ты умеешь без интернета".
    if (x.any({u"интернет", u"internet", u"online", u"сеть ", u"сети", u"wifi", u"wi-fi"})
        && x.any({u"есть ли", u"есть интернет", u"интернет есть", u"работает", u"подключ", u"доступ", u"состояни", u"почему", u"проверь",
                  u"is there", u"am i online", u"connected", u"working", u"status", u"why", u"check"})
        && !x.any({u"открой", u"open", u"найди", u"поищи", u"search", u"без интернет", u"without"})) {
        out->append(step(x.any({u"почему", u"why"}) ? u"system.diagnose"_s : u"system.status"_s, {{u"what"_s, u"network"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"почему", u"why"}) && x.any({u"окн", u"приложени", u"window", u"app"})) {
        out->append(step(u"system.diagnose"_s, {{u"what"_s, u"apps"_s}}));
        return Clause::Parsed;
    }

    // ---- Media ----
    if (x.any({u"пауз", u"pause"}) || (x.any({u"останови", u"stop"}) && x.any({u"музык", u"воспроизведен", u"видео", u"трек", u"music", u"playback"}))) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"pause"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"следующ", u"next track", u"next song", u"skip"})) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"next"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"предыдущ", u"previous track", u"previous song"})) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"previous"_s}}));
        return Clause::Parsed;
    }
    if (x.any({u"продолжи", u"возобнови", u"resume"}) && x.any({u"музык", u"воспроизведен", u"видео", u"music", u"playback"})) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"play"_s}}));
        return Clause::Parsed;
    }

    // ---- Workspaces (Jarvis profiles) ----
    const workspace::Command ws = workspace::parseCommand(clause);
    if (ws.kind == u"workspace" && !ws.prepare) {
        out->append(step(u"workspace.switch"_s, {{u"profile"_s, ws.arg}}));
        return Clause::Parsed;
    }
    if (!ws.kind.isEmpty() && ws.kind != u"open-app")
        return Clause::Unknown; // folders, projects, figures: the desktop handler answers those

    // ---- Applications ----
    // "его", "это окно", "it": the application the conversation is about (the agent fills it in).
    static const QRegularExpression pronoun = rx(
        u"^(?:(?:его|её|ее|это|этот|эту|него|нее|неё|it|this|that|them)(?:\\s+(?:окно|приложение|window|app))?|окно|window|the window)$"_s);
    auto object = [&](const QString &verb, QString *o) {
        *o = objectAfter(clause, {verb});
        static const QRegularExpression full = rx(u"\\s*(?:на\\s+(?:весь|полный)\\s+экран|во\\s+весь\\s+экран|(?:to\\s+)?full\\s*screen)$"_s);
        o->remove(full);
        *o = o->trimmed();
        if (o->isEmpty() || pronoun.match(*o).hasMatch()) {
            o->clear();
            p->pronoun = true;
        }
    };
    for (const QString &v : kMaximizeVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ') || clause == v) {
            QString o;
            object(v, &o);
            if (x.any({u"режим", u"профил", u"рабоч"}))
                return Clause::Unknown;
            out->append(step(u"app.focus"_s, {{u"query"_s, o.left(60)}, {u"maximize"_s, true}}));
            *lastVerb = u"app.focus"_s;
            return Clause::Parsed;
        }
    for (const QString &v : kFocusVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ')) {
            QString o;
            object(v, &o);
            if (x.any({u"режим", u"профил", u"рабоч"}) || (o.isEmpty() && !p->pronoun))
                return Clause::Unknown;
            out->append(step(u"app.focus"_s, {{u"query"_s, o.left(60)}}));
            *lastVerb = u"app.focus"_s;
            return Clause::Parsed;
        }
    for (const QString &v : kCloseVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ') || clause == v) {
            if (x.any({u"все окна", u"всё", u"все ", u"all windows", u"everything"}))
                return Clause::Unknown; // "close everything" is not something rules do
            QString o;
            object(v, &o);
            out->append(step(u"app.close"_s, {{u"query"_s, o.left(60)}}));
            *lastVerb = u"app.close"_s;
            return Clause::Parsed;
        }
    for (const QString &v : kOpenVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ')) {
            const QString o = objectAfter(clause, {v});
            if (o.isEmpty() || x.any({u"папк", u"folder", u"проект", u"project"}))
                return Clause::Unknown;
            // "запусти что-нибудь…" names no application: the model (or the user) decides.
            static const QRegularExpression vague = rx(u"^(?:что-?нибудь|что-?то|кое-?что|какое-?нибудь|что-?либо|something|anything)\\b"_s);
            if (vague.match(o).hasMatch() && genericAppKind(o).isEmpty())
                return Clause::Unknown;
            out->append(step(u"app.open"_s, {{u"query"_s, o.left(60)}}));
            *lastVerb = u"app.open"_s;
            return Clause::Parsed;
        }
    // "…открой Telegram и Firefox": a bare name continues the previous verb.
    static const QRegularExpression notAName = rx(u"^(?:чтобы|что|который|которая|если|когда|потому|так как|so that|which|that|if|when|because)\\b"_s);
    if (!lastVerb->isEmpty() && clause.split(u' ', Qt::SkipEmptyParts).size() <= 3 && !domain && !notAName.match(clause).hasMatch()) {
        out->append(step(*lastVerb, {{u"query"_s, clause.left(60)}}));
        return Clause::Parsed;
    }
    return Clause::Unknown;
}

} // namespace

QString normalizeCommand(const QString &text)
{
    QString t = text.toLower();
    t.replace(u'ё', u'е');
    // Keep %, digits, letters, hyphens and the separators the splitter needs.
    static const QRegularExpression junk = rx(u"[^\\p{L}\\p{N}%,;\\-\\s]+"_s);
    t.replace(junk, u" "_s);
    t = t.simplified();
    static const QRegularExpression name = rx(u"^(?:джарвис|jarvis|эй джарвис|hey jarvis)[,\\s]*"_s);
    t.remove(name);
    static const QRegularExpression polite = rx(u"^(?:пожалуйста|please|будь добр|будь другом)[,\\s]+"_s);
    t.remove(polite);
    return t.trimmed();
}

int parseNumber(const QString &words)
{
    static const QRegularExpression digits(u"(\\d{1,3})\\s*(?:%|процент|percent)?"_s);
    const auto m = digits.match(words);
    if (m.hasMatch())
        return m.captured(1).toInt();
    int total = -1;
    for (const QString &w : words.toLower().split(rx(u"[\\s,-]+"_s), Qt::SkipEmptyParts)) {
        const auto it = numberWords().constFind(w);
        if (it == numberWords().constEnd()) {
            if (total >= 0 && !w.startsWith(u"процент"_s) && !w.startsWith(u"percent"_s))
                break; // the number ended
            continue;
        }
        total = total < 0 ? it.value() : (it.value() == 100 && total < 10 ? total * 100 : total + it.value());
    }
    return total;
}

namespace {

// Clauses joined by "и", "потом", "но", commas → steps, things not understood, things declined.
void parseClauses(const QString &text, Parsed *p)
{
    static const QRegularExpression split = rx(
        u"\\s*(?:[,;]\\s*(?:а\\s+|и\\s+|но\\s+|потом\\s+|затем\\s+|then\\s+|and\\s+|but\\s+)?|\\s+(?:и|а также|потом|затем|после этого|а потом|но|однако|and then|then|and|but)\\s+)"_s);
    QString lastVerb;
    // "…, чтобы посмотреть сайты" belongs to the clause before it.
    QStringList clauses;
    static const QRegularExpression subordinate = rx(u"^(?:чтобы|что|который|которая|которое|если|когда|потому что|так как|пока|so that|which|that|if|when|because|while)\\b"_s);
    for (const QString &c : text.split(split, Qt::SkipEmptyParts)) {
        if (!clauses.isEmpty() && subordinate.match(c.trimmed()).hasMatch())
            clauses.last() += u' ' + c.trimmed();
        else
            clauses.append(c);
    }
    for (QString clause : clauses) {
        clause = clause.trimmed();
        static const QRegularExpression lead = rx(u"^(?:и|а|но|потом|затем|ещё|еще|сначала|также|then|and|also|but)\\s+"_s);
        clause.remove(lead);
        if (clause.isEmpty())
            continue;
        switch (parseClause(clause, p, &lastVerb)) {
        case Clause::Parsed:
            break;
        case Clause::Unknown:
            p->unparsed.append(clause);
            break;
        case Clause::Declined:
            p->declined.append(clause);
            break;
        }
    }
}

// A request to change something a tool changes — a thing on the laptop and a verb, an
// amount or a direction: "put sound on 20", "звук на двадцать", "поправь звук". It is for
// the agent, never for a chat that cannot act. "Расскажи про звук в кино" names the
// thing but asks for nothing to be done: the chat answers it.
bool namesActionThing(const QString &text)
{
    const Text x{u' ' + text + u' '};
    const bool thing = x.any({u"звук", u"громкост", u"громкаст", u"грамкост", u"тише", u"громче", u"наушник", u"гарнитур", u"динамик",
                              u"колонк", u"блютуз", u"блютус", u"bluetooth", u"яркост", u"ярче", u"темнее", u"микрофон", u"уведомлен",
                              u"не беспокоить", u"sound", u"volume", u"volum", u"audio", u"headphone", u"headset", u"earbud", u"speaker",
                              u"brightness", u"microphone", u"notification", u"do not disturb", u"mute", u"unmute", u"louder", u"quieter",
                              u"brighter", u"darker", u"hear", u"слышно"});
    if (!thing)
        return false;
    const bool verb = x.any({u"поставь", u"сделай", u"включи", u"выключи", u"убав", u"прибав", u"увелич", u"уменьш", u"переключ", u"верни",
                             u"подключ", u"отключ", u"выведи", u"измени", u"поменяй", u"сними", u"убери", u"сбавь", u"поправь", u"исправь",
                             u"настрой", u"заглуши", u"put ", u"set ", u"make ", u"turn ", u"switch ", u"change ", u"raise ", u"lower ",
                             u"increase", u"decrease", u"connect", u"disconnect", u"enable", u"disable", u"adjust", u"fix ", u"crank",
                             u"bring ", u"play ", u"mute", u"unmute", u"silence"});
    const bool amount = parseNumber(text) >= 0
                        || x.any({u"тише", u"потише", u"громче", u"погромче", u"ярче", u"темнее", u"louder", u"quieter", u"brighter", u"darker"})
                        || x.words({u"up", u"down", u"on", u"off"});
    return verb || amount;
}

// Quoted parts of the message (« », " ", “ ”, „ “) and what is left around them.
QStringList quoted(const QString &raw, QString *rest)
{
    static const QRegularExpression q(u"«([^»]{1,300})»|\"([^\"]{1,300})\"|“([^”]{1,300})”|„([^“”]{1,300})[“”]"_s);
    QStringList parts;
    *rest = raw;
    auto it = q.globalMatch(raw);
    while (it.hasNext()) {
        const auto m = it.next();
        parts.append(m.captured(1) + m.captured(2) + m.captured(3) + m.captured(4));
        rest->replace(m.captured(0), u" "_s);
    }
    return parts;
}

} // namespace

Parsed parse(const QString &raw)
{
    Parsed p;
    const QString text = normalizeCommand(raw);
    if (text.isEmpty())
        return p;
    const Text whole{u' ' + text + u' '};
    p.actionish = namesActionThing(text);

    // ---- Short continuations, only as the whole message ----
    static const QRegularExpression stop = rx(
        u"^(?:стоп|остановись|остановиcь|остановись пожалуйста|хватит|прекрати|отмена|отмени задачу|отмени это действие|стой|stop|cancel|abort|halt)$"_s);
    if (stop.match(text).hasMatch()) {
        p.followUp = u"stop"_s;
        return p;
    }
    static const QRegularExpression yes = rx(u"^(?:да|ага|угу|конечно|давай|подтверждаю|да закрой|да давай|yes|yeah|yep|ok|ок|окей|sure|confirm)$"_s);
    static const QRegularExpression no = rx(u"^(?:нет|не надо|не нужно|не|no|nope|don t)$"_s);
    if (yes.match(text).hasMatch()) {
        p.followUp = u"yes"_s;
        return p;
    }
    if (no.match(text).hasMatch()) {
        p.followUp = u"no"_s;
        return p;
    }
    static const QRegularExpression undo = rx(
        u"^(?:(?:верни|сделай|поставь)\\s+(?:все\\s+|всё\\s+)?(?:обратно|как было|назад|как раньше)(?:\\s+(?:до этого|раньше))?|как было|"
        u"отмени (?:это|последнее|изменения)|откати(?: изменения| это)?|undo(?: that| it)?|put it back|revert(?: that| it)?|change it back|"
        u"set it back|go back to how it was)$"_s);
    if (undo.match(text).hasMatch()) {
        p.followUp = u"undo"_s;
        return p;
    }
    static const QRegularExpression other = rx(
        u"^(?:(?:нет,? )?(?:другие|другой|другую|не эти|не те) (?:наушники|устройство|гарнитуру|колонку)|другие|the other (?:ones|headphones|device)|other headphones|not these)$"_s);
    if (other.match(text).hasMatch()) {
        p.followUp = u"other"_s;
        return p;
    }
    static const QRegularExpression choice = rx(
        u"^(?:(перв|втор|трет|четв)\\S*|([1-4])|(first|second|third|fourth))(?: (?:вариант|наушник\\S*|устройств\\S*|one|option|ones))?$"_s);
    if (const auto m = choice.match(text); m.hasMatch()) {
        static const QStringList ru{u"перв"_s, u"втор"_s, u"трет"_s, u"четв"_s};
        static const QStringList en{u"first"_s, u"second"_s, u"third"_s, u"fourth"_s};
        p.choice = !m.captured(1).isEmpty() ? int(ru.indexOf(m.captured(1)))
                   : !m.captured(2).isEmpty() ? m.captured(2).toInt() - 1
                                              : int(en.indexOf(m.captured(3)));
        p.followUp = u"choice"_s;
        return p;
    }
    // "нет, 30", "лучше 40", "давай на 25%", "make it 30": a new value for what was just set.
    static const QRegularExpression number = rx(
        u"^(?:(?:нет|не|no|nope|лучше|давай|а|ну|хм|actually|make it|rather|сделай|поставь)[,\\s]+)*(?:на|до|to|at)?\\s*((?:[\\p{L}\\d-]+\\s*){1,3}?)\\s*(?:%|процент\\S*|percent)?$"_s);
    if (const auto m = number.match(text); m.hasMatch()) {
        bool allNumber = true;
        for (const QString &w : m.captured(1).split(rx(u"[\\s-]+"_s), Qt::SkipEmptyParts))
            allNumber = allNumber && (numberWords().contains(w) || (w.toInt() > 0 || w == u"0"));
        const int n = parseNumber(m.captured(1));
        if (allNumber && n >= 0) {
            p.followUp = u"number"_s;
            p.number = n;
            return p;
        }
    }
    // "ещё тише", "сделай ещё потише": plain steps (the rules below), the step size is remembered by the tool.

    // ---- Not a request to act: questions, what-ifs, quotations ----
    QString inner;
    static const QRegularExpression ifAsked = rx(
        u"^(?:а\\s+)?(?:если|что если|что будет,? если|что произойдет,? если|if|what if|what happens if|what would happen if)\\s+(?:я\\s+)?(?:тебя\\s+|i\\s+)?"
        u"(?:попрошу|скажу|напишу|попросил|сказал|прикажу|ask|asked|tell|told|say|said)\\s+(?:тебя\\s+|you\\s+)?(?:to\\s+)?(.+?)(?:\\s*,?\\s*(?:что|what)\\s+.*)?$"_s);
    static const QRegularExpression whatWould = rx(
        u"^(?:а\\s+)?(?:что|what)\\s+(?:ты\\s+)?(?:сделаешь|будешь делать|would you do|will you do|do you do)\\s*,?\\s*(?:если|if|when|когда)\\s+(?:я\\s+)?(?:тебя\\s+|i\\s+)?"
        u"(?:попрошу|скажу|напишу|ask|tell|say)\\s+(?:тебя\\s+|you\\s+)?(?:to\\s+)?(.+)$"_s);
    // How-to questions: "как поставить громкость 20" (an infinitive), "how do I mute", "how to…".
    static const QRegularExpression howTo = rx(
        u"^(?:а\\s+)?(?:как|каким образом|где)\\s+(?:мне\\s+|можно\\s+|нужно\\s+|надо\\s+)?(\\S+(?:ть|ти|чь)(?:ся)?\\b.*)$|"
        u"^(?:how\\s+(?:do|can|could|should|would)\\s+(?:i|you|we|one)\\s+|how\\s+to\\s+|where\\s+(?:do|can)\\s+i\\s+)(.+)$|"
        u"^(?:что значит|что означает|что такое|what does)\\s+(.+?)(?:\\s+mean)?$|"
        u"^(?:можно ли|is it possible to|can i)\\s+(.+)$"_s);
    if (const auto m = ifAsked.match(text); m.hasMatch()) {
        p.mode = u"hypothetical"_s;
        inner = m.captured(1);
    } else if (const auto m2 = whatWould.match(text); m2.hasMatch()) {
        p.mode = u"hypothetical"_s;
        inner = m2.captured(1);
    } else if (const auto m3 = howTo.match(text); m3.hasMatch() && !whole.any({u"почему", u"why"})) {
        p.mode = u"question"_s;
        inner = m3.captured(1) + m3.captured(2) + m3.captured(3) + m3.captured(4);
    } else {
        QString rest;
        const QStringList parts = quoted(raw, &rest);
        rest = normalizeCommand(rest);
        static const QRegularExpression runIt = rx(u"^(?:выполни|сделай|do|run|execute)?$"_s);
        if (!parts.isEmpty() && !runIt.match(rest).hasMatch()) {
            p.mode = u"quote"_s;
            inner = normalizeCommand(parts.join(u", "_s));
        } else if (!parts.isEmpty()) {
            return parse(parts.join(u", "_s)); // `выполни "громкость 20"`: the quoted command itself
        }
    }
    if (!p.mode.isEmpty()) {
        Parsed what;
        parseClauses(inner, &what);
        p.described = what.steps;
        p.systemish = what.systemish || p.actionish;
        return p;
    }

    // ---- Clauses ----
    parseClauses(text, &p);
    p.systemish = p.systemish || whole.any({u"сделай так", u"настрой"});
    if (p.steps.isEmpty() && p.unparsed.isEmpty() && p.problems.isEmpty() && !p.declined.isEmpty())
        p.mode = u"negated"_s;
    return p;
}

bool isCommand(const QString &text)
{
    const Parsed p = parse(text);
    if (!p.mode.isEmpty())
        return false;
    return !p.steps.isEmpty() || !p.problems.isEmpty() || (p.actionish && p.followUp.isEmpty());
}

} // namespace jarvis::agent
