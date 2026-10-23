#include "driving_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

#include "memory_text.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

struct Theme
{
    const char *code;
    const char16_t *ru;
    const char16_t *en;
    QStringList words; // normalized words that name the theme (French, Russian, English)
};

const QList<Theme> &table()
{
    static const QList<Theme> list = {
        {"L", u"Дорожное законодательство (L)", u"Traffic law (L)",
         {u"l"_s, u"circulation"_s, u"dispositions legales"_s, u"legal"_s, u"law"_s, u"закон"_s, u"законодательство"_s,
          u"priorite"_s, u"priorité"_s, u"intersection"_s, u"перекресток"_s, u"перекрёсток"_s, u"приоритет"_s,
          u"signalisation"_s, u"panneau"_s, u"panneaux"_s, u"знак"_s, u"знаки"_s, u"signs"_s, u"vitesse"_s, u"скорость"_s,
          u"depassement"_s, u"dépassement"_s, u"обгон"_s, u"stationnement"_s, u"парковка"_s, u"стоянка"_s}},
        {"C", u"Водитель (C)", u"The driver (C)",
         {u"c"_s, u"conducteur"_s, u"driver"_s, u"водитель"_s, u"alcool"_s, u"алкоголь"_s, u"fatigue"_s, u"усталость"_s,
          u"medicaments"_s, u"médicaments"_s, u"лекарства"_s, u"telephone"_s, u"téléphone"_s, u"телефон"_s,
          u"vigilance"_s, u"perception"_s, u"drogues"_s, u"stupefiants"_s}},
        {"R", u"Дорога (R)", u"The road (R)",
         {u"r"_s, u"route"_s, u"road"_s, u"дорога"_s, u"autoroute"_s, u"автомагистраль"_s, u"nuit"_s, u"ночь"_s,
          u"pluie"_s, u"дождь"_s, u"neige"_s, u"снег"_s, u"brouillard"_s, u"туман"_s, u"virage"_s, u"поворот"_s,
          u"tunnel"_s, u"тоннель"_s, u"montagne"_s}},
        {"U", u"Другие участники движения (U)", u"Other road users (U)",
         {u"u"_s, u"usagers"_s, u"users"_s, u"pieton"_s, u"piéton"_s, u"пешеход"_s, u"пешеходы"_s, u"cycliste"_s,
          u"велосипедист"_s, u"moto"_s, u"мотоцикл"_s, u"poids lourd"_s, u"грузовик"_s, u"bus"_s, u"автобус"_s,
          u"tramway"_s, u"трамвай"_s, u"usagers vulnerables"_s}},
        {"D", u"Общие правила и разное (D)", u"General rules and misc (D)",
         {u"d"_s, u"divers"_s, u"notions diverses"_s, u"misc"_s, u"разное"_s, u"permis"_s, u"права"_s, u"points"_s,
          u"баллы"_s, u"probatoire"_s, u"assurance"_s, u"страховка"_s, u"constat"_s, u"documents"_s, u"документы"_s,
          u"accident"_s, u"дтп"_s}},
        {"A", u"Первая помощь (A)", u"First aid (A)",
         {u"a"_s, u"secours"_s, u"premiers secours"_s, u"first aid"_s, u"помощь"_s, u"первая помощь"_s,
          u"proteger alerter secourir"_s, u"alerter"_s, u"blesse"_s, u"blessé"_s, u"пострадавший"_s}},
        {"P", u"Посадка и выход из автомобиля (P)", u"Getting in and out of the vehicle (P)",
         {u"p"_s, u"precautions"_s, u"précautions"_s, u"quitter le vehicule"_s, u"descendre"_s, u"выход"_s, u"посадка"_s,
          u"portiere"_s, u"portière"_s, u"дверь"_s, u"ceinture"_s, u"ремень"_s, u"enfant"_s, u"ребенок"_s, u"ребёнок"_s}},
        {"M", u"Механика и безопасность (M)", u"Mechanics (M)",
         {u"m"_s, u"mecanique"_s, u"mécanique"_s, u"mechanics"_s, u"механика"_s, u"pneus"_s, u"шины"_s, u"freins"_s,
          u"тормоза"_s, u"voyant"_s, u"voyants"_s, u"индикатор"_s, u"huile"_s, u"масло"_s, u"batterie"_s, u"аккумулятор"_s,
          u"abs"_s, u"esp"_s}},
        {"S", u"Оборудование безопасности (S)", u"Safety equipment (S)",
         {u"s"_s, u"securite"_s, u"sécurité"_s, u"equipements"_s, u"équipements"_s, u"safety"_s, u"безопасность"_s,
          u"airbag"_s, u"feux"_s, u"фары"_s, u"eclairage"_s, u"éclairage"_s, u"gilet"_s, u"жилет"_s, u"triangle"_s,
          u"треугольник"_s, u"retroviseur"_s, u"rétroviseur"_s, u"зеркало"_s}},
        {"E", u"Экология и экономия (E)", u"Environment (E)",
         {u"e"_s, u"environnement"_s, u"environment"_s, u"экология"_s, u"eco conduite"_s, u"écoconduite"_s,
          u"ecoconduite"_s, u"consommation"_s, u"расход"_s, u"pollution"_s, u"загрязнение"_s, u"bruit"_s, u"шум"_s,
          u"crit air"_s, u"vignette"_s}},
    };
    return list;
}

QDate dayOf(double ms)
{
    return QDateTime::fromMSecsSinceEpoch(qint64(ms)).date();
}

} // namespace

DrivingStore::DrivingStore()
{
    load();
}

QString DrivingStore::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/driving.json"_s;
}

QStringList DrivingStore::themes()
{
    QStringList out;
    for (const Theme &t : table())
        out.append(QLatin1String(t.code));
    return out;
}

QString DrivingStore::themeName(const QString &code, Lang lang)
{
    for (const Theme &t : table())
        if (QLatin1String(t.code) == code)
            return QString::fromUtf16(lang == Lang::Ru ? t.ru : t.en);
    return code;
}

QString DrivingStore::themeOf(const QString &input)
{
    const QString value = input.trimmed();
    if (value.size() == 1 && themes().contains(value.toUpper()))
        return value.toUpper();
    const QString norm = text::normalize(value);
    if (norm.isEmpty())
        return {};
    for (const Theme &t : table())
        for (const QString &w : t.words)
            if (w.size() > 1 && (norm == text::normalize(w) || (u' ' + norm + u' ').contains(u' ' + text::normalize(w) + u' ')))
                return QLatin1String(t.code);
    return {};
}

void DrivingStore::load()
{
    m_root = {};
    QFile f(path());
    if (f.open(QIODevice::ReadOnly) && f.size() < 4 * 1024 * 1024)
        m_root = QJsonDocument::fromJson(f.readAll()).object();
}

bool DrivingStore::save() const
{
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(u"."_s))
        return false;
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    QJsonObject root = m_root;
    root[u"version"_s] = 1;
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return f.write(data) == data.size() && f.commit();
}

void DrivingStore::prune(qint64 nowMs)
{
    const double cutoff = double(nowMs) - double(kKeepDays) * 24 * 3600 * 1000;
    QJsonArray kept;
    for (const auto &v : m_root.value(u"history"_s).toArray())
        if (v.toObject().value(u"at"_s).toDouble() >= cutoff)
            kept.append(v);
    while (kept.size() > kMaxHistory)
        kept.removeFirst();
    m_root[u"history"_s] = kept;
}

bool DrivingStore::record(const QString &themeInput, const QString &result, qint64 nowMs)
{
    const QString theme = themeOf(themeInput);
    if (theme.isEmpty() || (result != u"right" && result != u"wrong" && result != u"seen"))
        return false;
    load(); // the daemon and a window without a daemon share the file
    QJsonObject totals = m_root.value(u"themes"_s).toObject();
    QJsonObject t = totals.value(theme).toObject();
    t[result] = t.value(result).toInt() + 1;
    t[u"last"_s] = double(nowMs);
    totals[theme] = t;
    m_root[u"themes"_s] = totals;
    QJsonArray history = m_root.value(u"history"_s).toArray();
    history.append(QJsonObject{{u"at"_s, double(nowMs)}, {u"theme"_s, theme}, {u"result"_s, result}});
    m_root[u"history"_s] = history;
    prune(nowMs);
    return save();
}

void DrivingStore::clear()
{
    m_root = {};
    QFile::remove(path());
}

QStringList DrivingStore::weakThemes(qint64 nowMs) const
{
    Q_UNUSED(nowMs)
    QList<std::pair<double, QString>> weak;
    const QJsonObject totals = m_root.value(u"themes"_s).toObject();
    for (auto it = totals.begin(); it != totals.end(); ++it) {
        const QJsonObject t = it.value().toObject();
        const int right = t.value(u"right"_s).toInt(), wrong = t.value(u"wrong"_s).toInt();
        const double accuracy = right + wrong > 0 ? double(right) / (right + wrong) : 1.0;
        if (wrong >= 2 && accuracy < 0.7)
            weak.append({accuracy, it.key()});
    }
    std::sort(weak.begin(), weak.end());
    QStringList out;
    for (const auto &w : weak)
        out.append(w.second);
    return out;
}

QJsonObject DrivingStore::stats(Lang lang, qint64 nowMs) const
{
    const QJsonObject totals = m_root.value(u"themes"_s).toObject();
    QJsonArray list;
    int answered = 0, right = 0;
    for (const QString &code : themes()) {
        const QJsonObject t = totals.value(code).toObject();
        const int r = t.value(u"right"_s).toInt(), w = t.value(u"wrong"_s).toInt();
        answered += r + w;
        right += r;
        list.append(QJsonObject{{u"code"_s, code}, {u"name"_s, themeName(code, lang)}, {u"seen"_s, t.value(u"seen"_s).toInt()},
                                {u"right"_s, r}, {u"wrong"_s, w}, {u"accuracy"_s, r + w > 0 ? double(r) / (r + w) : -1.0}});
    }
    // Right answers in a row, newest first.
    int streak = 0;
    const QJsonArray history = m_root.value(u"history"_s).toArray();
    for (qsizetype i = history.size() - 1; i >= 0; --i) {
        const QString result = history.at(i).toObject().value(u"result"_s).toString();
        if (result == u"seen")
            continue;
        if (result != u"right")
            break;
        ++streak;
    }
    int today = 0;
    for (const auto &v : history)
        if (dayOf(v.toObject().value(u"at"_s).toDouble()) == dayOf(double(nowMs)))
            ++today;
    return QJsonObject{{u"themes"_s, list}, {u"answered"_s, answered}, {u"right"_s, right},
                       {u"weak"_s, QJsonArray::fromStringList(weakThemes(nowMs))}, {u"streak"_s, streak}, {u"today"_s, today}};
}

QString DrivingStore::promptSummary(qint64 nowMs) const
{
    const QJsonObject s = stats(Lang::En, nowMs);
    const int answered = s.value(u"answered"_s).toInt();
    int seen = 0;
    for (const auto &v : s.value(u"themes"_s).toArray())
        seen += v.toObject().value(u"seen"_s).toInt();
    if (answered == 0 && seen == 0)
        return {};
    QString out = u"Code de la route practice: %1 own answers checked, %2 right."_s.arg(answered).arg(s.value(u"right"_s).toInt());
    QStringList weak;
    for (const auto &v : s.value(u"weak"_s).toArray())
        weak.append(themeName(v.toString(), Lang::En));
    if (!weak.isEmpty())
        out += u" Themes that keep going wrong: "_s + weak.join(u", "_s) + u"."_s;
    return out + u'\n';
}

QString DrivingStore::progressText(Lang lang, qint64 nowMs) const
{
    const bool ru = lang == Lang::Ru;
    const QJsonObject s = stats(lang, nowMs);
    const int answered = s.value(u"answered"_s).toInt();
    QStringList lines;
    for (const auto &v : s.value(u"themes"_s).toArray()) {
        const QJsonObject t = v.toObject();
        const int r = t.value(u"right"_s).toInt(), w = t.value(u"wrong"_s).toInt(), seen = t.value(u"seen"_s).toInt();
        if (r + w + seen == 0)
            continue;
        QString line = u"- **"_s + t.value(u"name"_s).toString() + u"**: "_s;
        if (r + w > 0)
            line += (ru ? u"%1 из %2 верно (%3 %)"_s : u"%1 of %2 right (%3 %)"_s)
                        .arg(r).arg(r + w).arg(qRound(100.0 * r / (r + w)));
        if (seen > 0)
            line += (r + w > 0 ? u"; "_s : QString()) + (ru ? u"разобрано вопросов: %1"_s : u"questions explained: %1"_s).arg(seen);
        lines.append(line);
    }
    if (lines.isEmpty())
        return ru ? u"Прогресса по Code de la route пока нет. Пришлите вопрос теста (скриншотом через Ctrl+J или текстом) "
                    u"и скажите, какой ответ выбрали, — я разберу его и начну вести статистику по темам."_s
                  : u"No Code de la route progress yet. Send me a test question (a screenshot through Ctrl+J or as text) and "
                    u"tell me which answer you picked — I'll go through it and start keeping statistics by theme."_s;
    QString out = (ru ? u"**Code de la route — ваш прогресс**\n\n"_s : u"**Code de la route — your progress**\n\n"_s) + lines.join(u'\n');
    if (answered > 0)
        out += (ru ? u"\n\nВсего проверено ваших ответов: %1, верно %2 %."_s : u"\n\nYour answers checked: %1, %2 % right."_s)
                   .arg(answered).arg(qRound(100.0 * s.value(u"right"_s).toInt() / answered));
    QStringList weak;
    for (const auto &v : s.value(u"weak"_s).toArray())
        weak.append(themeName(v.toString(), lang));
    if (!weak.isEmpty())
        out += (ru ? u"\n\nСтоит повторить: "_s : u"\n\nWorth revising: "_s) + weak.join(u", "_s) + u'.';
    if (s.value(u"streak"_s).toInt() >= 3)
        out += (ru ? u"\n\nВерных ответов подряд: %1."_s : u"\n\nRight answers in a row: %1."_s).arg(s.value(u"streak"_s).toInt());
    out += ru ? u"\n\nНа экзамене ETG нужно не меньше 35 верных из 40."_s : u"\n\nThe ETG exam needs at least 35 right out of 40."_s;
    return out;
}

} // namespace jarvis
