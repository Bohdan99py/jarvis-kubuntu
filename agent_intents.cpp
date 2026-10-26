#include "agent_intents.h"

#include <QHash>
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

int volumeNumber(const Text &x, const QString &clause)
{
    if (x.any({u"полную", u"максимум", u"максимальн", u"max", u"full"}))
        return 100;
    if (x.any({u"половин", u"наполовину", u"half"}))
        return 50;
    if (x.any({u"четверть", u"quarter"}))
        return 25;
    if (x.any({u"минимум", u"минимальн", u"min "}))
        return 5;
    return parseNumber(clause);
}

int relativeStep(const Text &x, int base)
{
    if (x.any({u"намного", u"сильно", u"гораздо", u"much ", u"a lot"}))
        return base * 2;
    if (x.any({u"чуть", u"немного", u"слегка", u"капельку", u"a bit", u"slightly", u"little"}))
        return base / 2;
    return base;
}

bool negated(const Text &x)
{
    // "не беспокоить" is a name; "не надо", "не включай" are not requests to act.
    QString t = x.t;
    t.replace(u" не беспокоить"_s, u" беспокоить"_s);
    t.replace(u" не отвлекали"_s, u" отвлекали"_s);
    t.replace(u" не слышно"_s, u" слышно"_s);
    return t.contains(u" не "_s) || t.contains(u" don't "_s) || t.contains(u" do not "_s) || t.contains(u" dont "_s);
}

const QStringList kOpenVerbs{u"открой"_s, u"открыть"_s, u"запусти"_s, u"запустить"_s, u"включи"_s, u"open"_s, u"launch"_s, u"start"_s, u"run"_s};
const QStringList kFocusVerbs{u"переключись на"_s, u"переключись в"_s, u"перейди в"_s, u"перейди на"_s, u"покажи окно"_s, u"разверни"_s, u"switch to"_s, u"focus"_s};
const QStringList kCloseVerbs{u"закрой"_s, u"закрыть"_s, u"выйди из"_s, u"close"_s, u"quit"_s, u"exit"_s};

// One clause → its steps; false when the rules do not understand it.
bool parseClause(const QString &clause, QList<Step> *out, bool *systemish, QString *lastVerb)
{
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
            return true;
        }
    }
    if (negated(x))
        return false;
    // "как включить блютуз?" asks how, it does not ask to do it.
    static const QRegularExpression question = rx(
        u"^(?:как|каким образом|зачем|что такое|что значит|где|можно ли|how|what is|what does|where|can i|is it possible)\\b"_s);
    if (question.match(clause).hasMatch() && !x.any({u"почему", u"why"}))
        return false;

    const bool soundWord = x.any({u"звук", u"аудио", u"музык", u"вывод", u"выход", u"sound", u"audio", u"music", u"output"});
    const bool headphones = x.any({u"наушник", u"гарнитур", u"airpods", u"headphone", u"headset", u"earbud", u"блютуз-уш"})
                            || x.words({u"уши", u"ушах", u"ушки", u"ушках", u"ушами"});
    const bool speakers = x.any({u"динамик", u"ноутбук", u"ноут ", u"встроенн", u"колонки ноутбука", u"speaker", u"laptop", u"built-in"});
    const bool mic = x.any({u"микрофон", u"microphone", u"mic "});
    const bool bt = x.any({u"блютуз", u"блютус", u"bluetooth"});
    const bool offVerb = x.any({u"выключи", u"отключи", u"выруби", u"turn off", u"switch off", u"disable", u"disconnect"});
    const bool onVerb = x.any({u"включи", u"вруби", u"turn on", u"switch on", u"enable"});

    // ---- Microphone ----
    if (mic) {
        if (x.any({u"какие", u"список", u"покажи", u"доступн", u"which", u"list", u"show"})) {
            out->append(step(u"audio.list"_s));
            return true;
        }
        if (x.any({u"встроенн", u"ноутбук", u"builtin", u"built-in", u"internal"})) {
            out->append(step(u"audio.input"_s, {{u"target"_s, u"builtin"_s}}));
            return true;
        }
        if (x.any({u"гарнитур", u"наушник", u"headset", u"headphone", u"bluetooth", u"блютуз"})) {
            out->append(step(u"audio.input"_s, {{u"target"_s, u"headset"_s}}));
            return true;
        }
        static const QRegularExpression named = rx(u"микрофон\\S*\\s+(?:на\\s+)?(.+)$|microphone\\s+(?:to\\s+)?(.+)$"_s);
        const auto m = named.match(clause);
        if (m.hasMatch() && x.any({u"выбери", u"переключи", u"поставь", u"choose", u"switch", u"use"})) {
            out->append(step(u"audio.input"_s, {{u"name"_s, (m.captured(1) + m.captured(2)).trimmed().left(60)}}));
            return true;
        }
        out->append(step(u"audio.list"_s));
        return true;
    }

    // ---- Headphones and speakers ----
    if (headphones) {
        if (x.any({u"нов", u"сопряг", u"сопряж", u"pair", u"new "})) {
            out->append(step(u"bluetooth.pair"_s));
            return true;
        }
        if (offVerb && !soundWord) {
            out->append(step(u"bluetooth.disconnect"_s, {{u"name"_s, headphonesName(clause)}}));
            return true;
        }
        if (x.any({u"почему", u"не слышно", u"не работа", u"why"})) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, u"audio"_s}}));
            return true;
        }
        QJsonObject args{{u"target"_s, u"headphones"_s}};
        if (const QString name = headphonesName(clause); !name.isEmpty())
            args[u"name"_s] = name;
        out->append(step(u"audio.output"_s, args));
        return true;
    }
    if (speakers && (soundWord || x.any({u"верни", u"переключи", u"выведи", u"switch", u"move", u"back"}))) {
        out->append(step(u"audio.output"_s, {{u"target"_s, u"speakers"_s}}));
        return true;
    }

    // "слишком громко", "экран слишком яркий": the opposite direction, one step.
    if (x.has(u"слишком") || x.has(u"too ")) {
        if (x.any({u"громк", u"loud"})) {
            out->append(step(u"audio.volume"_s, {{u"change"_s, -10}}));
            return true;
        }
        if (x.any({u"тихо", u"тихий", u"quiet"})) {
            out->append(step(u"audio.volume"_s, {{u"change"_s, 10}}));
            return true;
        }
        if (x.any({u"ярк", u"светл", u"bright"})) {
            out->append(step(u"screen.brightness"_s, {{u"change"_s, -15}}));
            return true;
        }
        if (x.any({u"темн", u"тускл", u"dark", u"dim"})) {
            out->append(step(u"screen.brightness"_s, {{u"change"_s, 15}}));
            return true;
        }
    }

    // ---- Volume and mute ----
    const bool volumeWord = x.any({u"громкост", u"volume"});
    if (x.any({u"без звука", u"замьют", u"mute"}) && !x.has(u"unmute")) {
        out->append(step(u"audio.mute"_s, {{u"mute"_s, true}}));
        return true;
    }
    if (x.has(u"unmute") || (onVerb && x.has(u"звук") && !volumeWord && parseNumber(clause) < 0)) {
        out->append(step(u"audio.mute"_s, {{u"mute"_s, false}}));
        return true;
    }
    if (offVerb && x.has(u"звук") && !volumeWord) {
        out->append(step(u"audio.mute"_s, {{u"mute"_s, true}}));
        return true;
    }
    if (x.any({u"тише", u"потише", u"убавь", u"уменьши громк", u"убери громк", u"quieter", u"turn down", u"lower the volume", u"volume down"})) {
        out->append(step(u"audio.volume"_s, {{u"change"_s, -relativeStep(x, 10)}}));
        return true;
    }
    if (x.any({u"громче", u"погромче", u"прибавь", u"увеличь громк", u"louder", u"turn up", u"volume up", u"raise the volume"})) {
        out->append(step(u"audio.volume"_s, {{u"change"_s, relativeStep(x, 10)}}));
        return true;
    }
    if (volumeWord || (x.has(u"звук") && parseNumber(clause) >= 0)) {
        const int n = volumeNumber(x, clause);
        if (n >= 0 && n <= 150) {
            out->append(step(u"audio.volume"_s, {{u"level"_s, n}}));
            return true;
        }
        if (x.any({u"какая", u"сколько", u"what", u"how"})) {
            out->append(step(u"audio.list"_s));
            return true;
        }
        return false;
    }

    // ---- Bluetooth ----
    if (bt) {
        if (x.any({u"почему", u"не работа", u"why"})) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, u"bluetooth"_s}}));
            return true;
        }
        if (x.any({u"состояни", u"статус", u"какие", u"что подключ", u"status", u"which", u"what"})) {
            out->append(step(u"bluetooth.status"_s));
            return true;
        }
        if (x.any({u"сопряг", u"сопряж", u"pair"})) {
            out->append(step(u"bluetooth.pair"_s));
            return true;
        }
        if (offVerb) {
            out->append(step(u"bluetooth.power"_s, {{u"on"_s, false}}));
            return true;
        }
        if (onVerb) {
            out->append(step(u"bluetooth.power"_s, {{u"on"_s, true}}));
            return true;
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
            return true;
        }
    }
    if (x.any({u"отключи", u"disconnect"}) && !x.any({u"интернет", u"сеть", u"wifi", u"уведомлен", u"звук", u"notification", u"sound"})) {
        const QString n = objectAfter(clause, {u"отключи"_s, u"disconnect"_s});
        if (!n.isEmpty()) {
            out->append(step(u"bluetooth.disconnect"_s, {{u"name"_s, n.left(60)}}));
            return true;
        }
    }

    // ---- Brightness ----
    if (x.any({u"яркост", u"brightness"})) {
        if (x.any({u"почему", u"why"})) {
            out->append(step(u"system.diagnose"_s, {{u"what"_s, u"brightness"_s}}));
            return true;
        }
        const int n = volumeNumber(x, clause);
        if (n >= 1 && n <= 100) {
            out->append(step(u"screen.brightness"_s, {{u"level"_s, n}}));
            return true;
        }
        if (x.any({u"убавь", u"уменьши", u"пониже", u"lower", u"down", u"decrease"})) {
            out->append(step(u"screen.brightness"_s, {{u"change"_s, -relativeStep(x, 15)}}));
            return true;
        }
        if (x.any({u"прибавь", u"увеличь", u"повыше", u"raise", u"up", u"increase"})) {
            out->append(step(u"screen.brightness"_s, {{u"change"_s, relativeStep(x, 15)}}));
            return true;
        }
        return false;
    }
    if (x.any({u"ярче", u"светлее", u"brighter"})) {
        out->append(step(u"screen.brightness"_s, {{u"change"_s, relativeStep(x, 15)}}));
        return true;
    }
    if (x.any({u"темнее", u"потемнее", u"тусклее", u"darker", u"dimmer", u"dim the"})) {
        out->append(step(u"screen.brightness"_s, {{u"change"_s, -relativeStep(x, 15)}}));
        return true;
    }

    // ---- Do not disturb ----
    if (x.any({u"не беспокоить", u"do not disturb", u"dnd", u"тихий режим", u"без уведомлений", u"не отвлекали", u"не отвлекай"})) {
        out->append(step(u"notifications.dnd"_s, {{u"on"_s, !(offVerb || x.any({u"выйди", u"сними", u"убери"}))}}));
        return true;
    }
    if (x.has(u"уведомлен") || x.has(u"notification")) {
        if (x.any({u"настройк", u"settings"})) {
            out->append(step(u"settings.open"_s, {{u"page"_s, u"notifications"_s}}));
            return true;
        }
        if (offVerb || x.any({u"убери", u"заглуши", u"silence"})) {
            out->append(step(u"notifications.dnd"_s, {{u"on"_s, true}}));
            return true;
        }
        if (onVerb || x.any({u"верни"})) {
            out->append(step(u"notifications.dnd"_s, {{u"on"_s, false}}));
            return true;
        }
    }

    // ---- Settings pages ----
    if (x.any({u"настройк", u"параметр", u"settings"})) {
        if (x.any({u"джарвис", u"jarvis"}))
            return false; // Jarvis's own settings: the window handles that
        const QString key = workspace::settingsKey(clause);
        if (!key.isEmpty()) {
            out->append(step(u"settings.open"_s, {{u"page"_s, key}}));
            return true;
        }
        return false;
    }

    // ---- Power, battery, network ----
    if (x.any({u"энергосбережен", u"экономи", u"power saver", u"power-saver", u"battery saver"}) && !x.any({u"почему", u"why"})) {
        out->append(step(u"power.profile"_s, {{u"profile"_s, offVerb ? u"balanced"_s : u"power-saver"_s}}));
        return true;
    }
    if (x.any({u"производительн", u"performance", u"максимальн мощност"})) {
        out->append(step(u"power.profile"_s, {{u"profile"_s, offVerb ? u"balanced"_s : u"performance"_s}}));
        return true;
    }
    if (x.any({u"сбалансирован", u"balanced", u"обычный режим питания"})) {
        out->append(step(u"power.profile"_s, {{u"profile"_s, u"balanced"_s}}));
        return true;
    }
    if (x.any({u"батаре", u"заряд", u"аккумулятор", u"battery"})) {
        out->append(step(u"system.status"_s, {{u"what"_s, u"battery"_s}}));
        return true;
    }
    if (x.any({u"профиль питания", u"режим питания", u"power profile"})) {
        out->append(step(u"system.status"_s, {{u"what"_s, u"power"_s}}));
        return true;
    }
    // "есть ли интернет", not "что ты умеешь без интернета".
    if (x.any({u"интернет", u"internet", u"online", u"сеть ", u"сети", u"wifi", u"wi-fi"})
        && x.any({u"есть ли", u"есть интернет", u"интернет есть", u"работает", u"подключ", u"доступ", u"состояни", u"почему", u"проверь",
                  u"is there", u"am i online", u"connected", u"working", u"status", u"why", u"check"})
        && !x.any({u"открой", u"open", u"найди", u"поищи", u"search", u"без интернет", u"without"})) {
        out->append(step(x.any({u"почему", u"why"}) ? u"system.diagnose"_s : u"system.status"_s, {{u"what"_s, u"network"_s}}));
        return true;
    }
    if (x.any({u"почему", u"why"}) && x.any({u"окн", u"приложени", u"window", u"app"})) {
        out->append(step(u"system.diagnose"_s, {{u"what"_s, u"apps"_s}}));
        return true;
    }

    // ---- Media ----
    if (x.any({u"пауз", u"pause"}) || (x.any({u"останови", u"stop"}) && x.any({u"музык", u"воспроизведен", u"видео", u"трек", u"music", u"playback"}))) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"pause"_s}}));
        return true;
    }
    if (x.any({u"следующ", u"next track", u"next song", u"skip"})) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"next"_s}}));
        return true;
    }
    if (x.any({u"предыдущ", u"previous track", u"previous song"})) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"previous"_s}}));
        return true;
    }
    if (x.any({u"продолжи", u"возобнови", u"resume"}) && x.any({u"музык", u"воспроизведен", u"видео", u"music", u"playback"})) {
        out->append(step(u"media.control"_s, {{u"action"_s, u"play"_s}}));
        return true;
    }

    // ---- Workspaces (Jarvis profiles) ----
    const workspace::Command ws = workspace::parseCommand(clause);
    if (ws.kind == u"workspace" && !ws.prepare) {
        out->append(step(u"workspace.switch"_s, {{u"profile"_s, ws.arg}}));
        return true;
    }
    if (!ws.kind.isEmpty() && ws.kind != u"open-app")
        return false; // folders, projects, figures: the desktop handler answers those

    // ---- Applications ----
    for (const QString &v : kFocusVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ')) {
            const QString o = objectAfter(clause, {v});
            if (o.isEmpty() || x.any({u"режим", u"профил", u"рабоч"}))
                return false;
            out->append(step(u"app.focus"_s, {{u"query"_s, o.left(60)}}));
            *lastVerb = u"app.focus"_s;
            return true;
        }
    for (const QString &v : kCloseVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ')) {
            const QString o = objectAfter(clause, {v});
            if (o.isEmpty() || x.any({u"все окна", u"всё", u"все ", u"all windows", u"everything"}))
                return false; // "close everything" is not something rules do
            out->append(step(u"app.close"_s, {{u"query"_s, o.left(60)}}));
            *lastVerb = u"app.close"_s;
            return true;
        }
    for (const QString &v : kOpenVerbs)
        if (clause.startsWith(v + u' ') || clause.contains(u' ' + v + u' ')) {
            const QString o = objectAfter(clause, {v});
            if (o.isEmpty() || x.any({u"папк", u"folder", u"проект", u"project"}))
                return false;
            // "запусти что-нибудь…" names no application: the model (or the user) decides.
            static const QRegularExpression vague = rx(u"^(?:что-?нибудь|что-?то|кое-?что|какое-?нибудь|что-?либо|something|anything)\\b"_s);
            if (vague.match(o).hasMatch() && genericAppKind(o).isEmpty())
                return false;
            out->append(step(u"app.open"_s, {{u"query"_s, o.left(60)}}));
            *lastVerb = u"app.open"_s;
            return true;
        }
    // "…открой Telegram и Firefox": a bare name continues the previous verb.
    static const QRegularExpression notAName = rx(u"^(?:чтобы|что|который|которая|если|когда|потому|так как|so that|which|that|if|when|because)\\b"_s);
    if (!lastVerb->isEmpty() && clause.split(u' ', Qt::SkipEmptyParts).size() <= 3 && !domain && !notAName.match(clause).hasMatch()) {
        out->append(step(*lastVerb, {{u"query"_s, clause.left(60)}}));
        return true;
    }
    return false;
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

Parsed parse(const QString &raw)
{
    Parsed p;
    const QString text = normalizeCommand(raw);
    if (text.isEmpty())
        return p;
    const Text whole{u' ' + text + u' '};

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
        u"^(?:верни (?:все |всё )?(?:обратно|как было|назад)|верни как было|отмени (?:это|последнее|изменения)|откати(?: изменения)?|undo|put it back|revert(?: that)?|change it back)$"_s);
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
        u"^(?:(перв|втор|трет|четв|1|2|3|4)\\S*|(first|second|third|fourth))(?: (?:вариант|наушник\\S*|устройств\\S*|one|option))?$"_s);
    if (const auto m = choice.match(text); m.hasMatch()) {
        static const QStringList ru{u"перв"_s, u"втор"_s, u"трет"_s, u"четв"_s};
        static const QStringList en{u"first"_s, u"second"_s, u"third"_s, u"fourth"_s};
        const QString c = m.captured(1);
        p.choice = !c.isEmpty() ? (c.at(0).isDigit() ? c.toInt() - 1 : int(ru.indexOf(c))) : int(en.indexOf(m.captured(2)));
        p.followUp = u"choice"_s;
        return p;
    }
    // "ещё тише", "сделай ещё потише": plain steps (the rules below), the step size is remembered by the tool.

    // ---- Clauses ----
    static const QRegularExpression split = rx(
        u"\\s*(?:[,;]\\s*(?:а\\s+|и\\s+|потом\\s+|затем\\s+|then\\s+|and\\s+)?|\\s+(?:и|а также|потом|затем|после этого|а потом|and then|then|and)\\s+)"_s);
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
        static const QRegularExpression lead = rx(u"^(?:и|а|потом|затем|ещё|еще|сначала|также|then|and|also)\\s+"_s);
        clause.remove(lead);
        if (clause.isEmpty())
            continue;
        if (!parseClause(clause, &p.steps, &p.systemish, &lastVerb))
            p.unparsed.append(clause);
    }
    p.systemish = p.systemish || whole.any({u"сделай так", u"настрой"});
    return p;
}

} // namespace jarvis::agent
