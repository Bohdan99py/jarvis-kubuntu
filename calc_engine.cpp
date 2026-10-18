#include "calc_engine.h"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QtAlgorithms>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

QRegularExpression rx(const QString &pattern, bool caseless = true)
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (caseless)
        options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, options);
}

// ---- Units ------------------------------------------------------------------

struct UnitName
{
    const char16_t *ru;
    const char16_t *en;
    bool prefixes;
};

const QHash<QString, UnitName> &units()
{
    static const QHash<QString, UnitName> table = {
        {u"V"_s, {u"В", u"V", true}},          {u"A"_s, {u"А", u"A", true}},
        {u"Ohm"_s, {u"Ом", u"Ω", true}},       {u"W"_s, {u"Вт", u"W", true}},
        {u"F"_s, {u"Ф", u"F", true}},          {u"H"_s, {u"Гн", u"H", true}},
        {u"Hz"_s, {u"Гц", u"Hz", true}},       {u"s"_s, {u"с", u"s", true}},
        {u"J"_s, {u"Дж", u"J", true}},         {u"N"_s, {u"Н", u"N", true}},
        {u"C"_s, {u"Кл", u"C", true}},         {u"m"_s, {u"м", u"m", false}},
        {u"kg"_s, {u"кг", u"kg", false}},      {u"m/s"_s, {u"м/с", u"m/s", false}},
        {u"m/s2"_s, {u"м/с²", u"m/s²", false}}, {u"J/kgK"_s, {u"Дж/(кг·°C)", u"J/(kg·°C)", false}},
        {u"K"_s, {u"°C", u"°C", false}},       {u"mAh"_s, {u"мА·ч", u"mAh", false}},
        {u"%"_s, {u"%", u"%", false}},         {u"dB"_s, {u"дБ", u"dB", false}},
        {u"dBm"_s, {u"дБм", u"dBm", false}},   {u"mm2"_s, {u"мм²", u"mm²", false}},
        {u"rho"_s, {u"Ом·мм²/м", u"Ω·mm²/m", false}}, {u"h"_s, {u"ч", u"h", false}},
        {u"bit"_s, {u"бит", u"bit", false}},   {u""_s, {u"", u"", true}},
        {u"n"_s, {u"", u"", false}},           {u"kWh"_s, {u"кВт·ч", u"kWh", false}},
        {u"text"_s, {u"", u"", false}},
    };
    return table;
}

QString unitName(const QString &unit, Lang lang)
{
    const auto it = units().find(unit);
    if (it == units().end())
        return unit;
    return QString::fromUtf16(lang == Lang::Ru ? it->ru : it->en);
}

QString number(double value, int digits, Lang lang)
{
    if (!std::isfinite(value))
        return u"—"_s;
    if (value == 0)
        return u"0"_s;
    const int magnitude = int(std::floor(std::log10(std::fabs(value))));
    const int decimals = std::clamp(digits - 1 - magnitude, 0, 9);
    QString s = QString::number(value, 'f', decimals);
    if (s.contains(u'.')) {
        while (s.endsWith(u'0'))
            s.chop(1);
        if (s.endsWith(u'.'))
            s.chop(1);
    }
    if (lang == Lang::Ru)
        s.replace(u'.', u',');
    return s;
}

// ---- Parsing quantities ---------------------------------------------------------

double prefixFactor(const QString &p)
{
    static const QHash<QString, double> table = {
        {u"п"_s, 1e-12}, {u"p"_s, 1e-12}, {u"н"_s, 1e-9}, {u"n"_s, 1e-9}, {u"мк"_s, 1e-6}, {u"µ"_s, 1e-6},
        {u"μ"_s, 1e-6}, {u"u"_s, 1e-6}, {u"м"_s, 1e-3}, {u"m"_s, 1e-3}, {u"к"_s, 1e3}, {u"k"_s, 1e3},
        {u"K"_s, 1e3}, {u"К"_s, 1e3}, {u"М"_s, 1e6}, {u"M"_s, 1e6}, {u"Г"_s, 1e9}, {u"G"_s, 1e9},
        {u"R"_s, 1}, {u"r"_s, 1}, {u""_s, 1}};
    return table.value(p, kNaN);
}

// A value with an optional prefix and unit; `expected` decides what a lone
// "m"/"м" means (metres for lengths, milli otherwise).
bool parseQuantity(QString text, const QString &expected, double *value)
{
    text = text.trimmed();
    text.remove(u' ');
    text.replace(u'−', u'-');
    if (text.isEmpty())
        return false;
    double scale = 1;
    // Units with their own scale.
    if (expected == u"m") {
        static const QList<std::pair<QString, double>> lengths = {
            {u"мм"_s, 1e-3}, {u"mm"_s, 1e-3}, {u"см"_s, 1e-2}, {u"cm"_s, 1e-2}, {u"км"_s, 1e3}, {u"km"_s, 1e3},
            {u"м"_s, 1}, {u"m"_s, 1}};
        for (const auto &[suffix, factor] : lengths)
            if (text.endsWith(suffix)) {
                text.chop(suffix.size());
                scale = factor;
                break;
            }
    } else if (expected == u"kg") {
        if (text.endsWith(u"кг") || text.endsWith(u"kg"))
            text.chop(2);
        else if (text.endsWith(u"г") || text.endsWith(u"g")) {
            text.chop(1);
            scale = 1e-3;
        }
    } else if (expected == u"mAh") {
        if (text.endsWith(u"мА·ч") || text.endsWith(u"мАч") || text.endsWith(u"mAh", Qt::CaseInsensitive))
            text.chop(text.endsWith(u"мА·ч") ? 4 : 3);
        else if (text.endsWith(u"Ач") || text.endsWith(u"Ah", Qt::CaseInsensitive)) {
            text.chop(2);
            scale = 1000;
        }
    } else if (expected == u"h") {
        if (text.endsWith(u"ч") || text.endsWith(u"h"))
            text.chop(1);
    } else {
        // Unit words are dropped; their prefixes stay.
        static const QStringList words = {u"Ом·мм²/м"_s, u"°C"_s, u"°С"_s, u"м/с²"_s, u"м/с2"_s, u"м/с"_s, u"m/s2"_s,
                                          u"m/s"_s, u"ohms"_s, u"ohm"_s, u"Ом"_s, u"Ω"_s, u"Вт"_s, u"Гн"_s, u"Гц"_s,
                                          u"Hz"_s, u"Дж"_s, u"Кл"_s, u"дБм"_s, u"dBm"_s, u"дБ"_s, u"dB"_s, u"бит"_s,
                                          u"bits"_s, u"bit"_s, u"сек"_s, u"sec"_s, u"%"_s, u"мм²"_s, u"mm2"_s, u"мм2"_s};
        for (const QString &w : words)
            if (text.endsWith(w, Qt::CaseInsensitive)) {
                text.chop(w.size());
                break;
            }
        // One-letter units: only when something stays in front of them.
        static const QHash<QString, QStringList> letters = {
            {u"V"_s, {u"В"_s, u"в"_s, u"V"_s, u"v"_s}}, {u"A"_s, {u"А"_s, u"а"_s, u"A"_s, u"a"_s}},
            {u"W"_s, {u"W"_s, u"w"_s}}, {u"F"_s, {u"Ф"_s, u"ф"_s, u"F"_s, u"f"_s}}, {u"H"_s, {u"H"_s}},
            {u"s"_s, {u"с"_s, u"s"_s}}, {u"J"_s, {u"J"_s}}, {u"N"_s, {u"Н"_s, u"N"_s}}, {u"C"_s, {u"C"_s}}};
        const QStringList all = expected.isEmpty()
                                    ? QStringList{u"В"_s, u"в"_s, u"V"_s, u"А"_s, u"A"_s, u"Ф"_s, u"ф"_s, u"F"_s, u"W"_s}
                                    : letters.value(expected);
        for (const QString &l : all)
            if (text.size() > 1 && text.endsWith(l) && !text.at(text.size() - 2).isSpace()) {
                // "10м" for amps is milli, so only the unit letter goes.
                text.chop(1);
                break;
            }
    }
    // 4k7, 2R2, 4n7: the letter is both the decimal point and the multiplier.
    static const QRegularExpression notation(u"^([-+]?\\d+)(мк|[pnuµμmkKMGRrпнмкКМГ])(\\d+)$"_s);
    if (const auto m = notation.match(text); m.hasMatch()) {
        const double f = prefixFactor(m.captured(2));
        if (std::isnan(f))
            return false;
        *value = (m.captured(1) + u'.' + m.captured(3)).toDouble() * f * scale;
        return true;
    }
    static const QRegularExpression plain(u"^([-+]?(?:\\d+(?:[.,]\\d*)?|[.,]\\d+)(?:[eE][-+]?\\d+)?)(мк|[pnuµμmkKMGпнмкКМГ])?$"_s);
    const auto m = plain.match(text);
    if (!m.hasMatch())
        return false;
    const double f = prefixFactor(m.captured(2));
    if (std::isnan(f))
        return false;
    *value = QString(m.captured(1)).replace(u',', u'.').toDouble() * f * scale;
    return std::isfinite(*value);
}

// ---- Resistor colours ----------------------------------------------------------------

struct Colour
{
    const char16_t *ru;
    const char16_t *en;
    int digit;
    double tolerance;
};

const QList<Colour> &colours()
{
    static const QList<Colour> list = {
        {u"чёрный", u"black", 0, -1},    {u"коричневый", u"brown", 1, 1}, {u"красный", u"red", 2, 2},
        {u"оранжевый", u"orange", 3, -1}, {u"жёлтый", u"yellow", 4, -1},  {u"зелёный", u"green", 5, 0.5},
        {u"синий", u"blue", 6, 0.25},     {u"фиолетовый", u"violet", 7, 0.1}, {u"серый", u"grey", 8, 0.05},
        {u"белый", u"white", 9, -1},      {u"золотой", u"gold", -1, 5},   {u"серебряный", u"silver", -2, 10}};
    return list;
}

int colourIndex(const QString &word)
{
    const QString w = QString(word).toLower().replace(u'ё', u'е');
    static const QList<std::pair<QString, int>> stems = {
        {u"черн"_s, 0}, {u"black"_s, 0}, {u"коричн"_s, 1}, {u"brown"_s, 1}, {u"красн"_s, 2}, {u"red"_s, 2},
        {u"оранж"_s, 3}, {u"orange"_s, 3}, {u"желт"_s, 4}, {u"yellow"_s, 4}, {u"зелен"_s, 5}, {u"green"_s, 5},
        {u"син"_s, 6}, {u"blue"_s, 6}, {u"фиолет"_s, 7}, {u"violet"_s, 7}, {u"purple"_s, 7}, {u"сер"_s, 8},
        {u"grey"_s, 8}, {u"gray"_s, 8}, {u"бел"_s, 9}, {u"white"_s, 9}, {u"золот"_s, 10}, {u"gold"_s, 10},
        {u"серебр"_s, 11}, {u"silver"_s, 11}};
    // "серебряный" before "серый": the longer stem wins.
    if (w.startsWith(u"серебр"))
        return 11;
    for (const auto &[stem, index] : stems)
        if (w.startsWith(stem))
            return index;
    return -1;
}

QString colourName(int index, Lang lang)
{
    const Colour &c = colours().at(index);
    return QString::fromUtf16(lang == Lang::Ru ? c.ru : c.en);
}

// Bands for a value: `digits` significant digits and a multiplier band.
QString bands(double value, int digits, Lang lang)
{
    if (!(value > 0))
        return {};
    int exponent = int(std::floor(std::log10(value))) - (digits - 1);
    long long significant = std::llround(value / std::pow(10.0, exponent));
    if (significant >= std::pow(10.0, digits)) {
        significant /= 10;
        ++exponent;
    }
    if (exponent < -2 || exponent > 9)
        return {};
    QStringList names;
    const QString text = QString::number(significant).rightJustified(digits, u'0');
    for (const QChar ch : text)
        names.append(colourName(ch.digitValue(), lang));
    names.append(exponent >= 0 ? colourName(exponent, lang) : colourName(exponent == -1 ? 10 : 11, lang));
    return names.join(u" — "_s);
}

// ---- Calculator table --------------------------------------------------------------

struct Input
{
    const char *id;
    const char16_t *ru;
    const char16_t *en;
    const char *unit;
    const char *value;
};

struct Calc
{
    const char *id;
    const char16_t *ru;
    const char16_t *en;
    const char16_t *descRu;
    const char16_t *descEn;
    const char16_t *formula;
    QList<Input> inputs;
};

const QList<Calc> &calcs()
{
    static const QList<Calc> list = {
        {"ohm", u"Закон Ома и мощность", u"Ohm's law and power",
         u"Заполните любые две величины — остальные посчитаются.", u"Fill in any two values to get the others.",
         u"U = I·R   P = U·I = I²·R = U²/R",
         {{"u", u"Напряжение U", u"Voltage U", "V", "5"}, {"i", u"Ток I", u"Current I", "A", ""},
          {"r", u"Сопротивление R", u"Resistance R", "Ohm", "220"}, {"p", u"Мощность P", u"Power P", "W", ""}}},
        {"led", u"Резистор для светодиода", u"LED resistor",
         u"Токоограничивающий резистор и его мощность.", u"Current-limiting resistor and its power rating.",
         u"R = (Uпит − n·Uпр) / I",
         {{"vs", u"Напряжение питания", u"Supply voltage", "V", "5"}, {"vf", u"Прямое падение на светодиоде", u"LED forward voltage", "V", "2"},
          {"if", u"Ток светодиода", u"LED current", "A", "20m"}, {"n", u"Светодиодов последовательно", u"LEDs in series", "n", "1"}}},
        {"divider", u"Делитель напряжения", u"Voltage divider",
         u"Uвых, ток и эквивалент Тевенина. Оставьте R1 или R2 пустым и задайте Uвых — подберу резистор.",
         u"Vout, current and the Thévenin equivalent. Leave R1 or R2 empty and set Vout to find it.",
         u"Uвых = Uвх·R2/(R1+R2)   Rth = R1‖R2",
         {{"vin", u"Входное напряжение", u"Input voltage", "V", "12"}, {"r1", u"R1 (верхний)", u"R1 (top)", "Ohm", "10k"},
          {"r2", u"R2 (нижний)", u"R2 (bottom)", "Ohm", "4.7k"}, {"vout", u"Нужное Uвых (необязательно)", u"Wanted Vout (optional)", "V", ""}}},
        {"series", u"Последовательно и параллельно", u"Series and parallel",
         u"Несколько номиналов через запятую: резисторы, катушки или конденсаторы.",
         u"Several values separated by commas: resistors, inductors or capacitors.",
         u"R = R1+R2+…   1/R = 1/R1+1/R2+…   (у C наоборот)",
         {{"values", u"Номиналы", u"Values", "text", "10k, 22k, 4.7k"}}},
        {"rc", u"RC-цепь", u"RC circuit",
         u"Постоянная времени, частота среза фильтра, время заряда.", u"Time constant, filter cutoff, charge time.",
         u"τ = R·C   fср = 1/(2π·R·C)",
         {{"r", u"Сопротивление", u"Resistance", "Ohm", "10k"}, {"c", u"Ёмкость", u"Capacitance", "F", "100n"}}},
        {"reactance", u"Реактивное сопротивление и резонанс", u"Reactance and resonance",
         u"Xc и XL на частоте, резонанс LC-контура.", u"Xc and XL at a frequency, LC resonance.",
         u"Xc = 1/(2πfC)   XL = 2πfL   f0 = 1/(2π√(LC))",
         {{"f", u"Частота", u"Frequency", "Hz", "1k"}, {"c", u"Ёмкость", u"Capacitance", "F", "100n"},
          {"l", u"Индуктивность", u"Inductance", "H", "10m"}}},
        {"color", u"Цветовая маркировка резисторов", u"Resistor colour code",
         u"Номинал → цвета полос, или цвета через пробел → номинал.", u"Value → band colours, or colours separated by spaces → value.",
         u"полосы: цифры, множитель, допуск",
         {{"value", u"Номинал или цвета", u"Value or colours", "text", "4.7k"}}},
        {"timer555", u"NE555 — мультивибратор", u"NE555 astable",
         u"Частота и скважность генератора на 555.", u"Frequency and duty cycle of a 555 oscillator.",
         u"f = 1,44 / ((R1 + 2·R2)·C)",
         {{"r1", u"R1", u"R1", "Ohm", "10k"}, {"r2", u"R2", u"R2", "Ohm", "100k"}, {"c", u"C", u"C", "F", "10u"}}},
        {"opamp", u"Операционный усилитель", u"Op-amp gain",
         u"Коэффициент усиления инвертирующего и неинвертирующего включения.", u"Gain of the inverting and non-inverting stages.",
         u"Kинв = −Rос/Rвх   Kнеинв = 1 + Rос/Rвх",
         {{"rf", u"Rос (обратная связь)", u"Rf (feedback)", "Ohm", "100k"}, {"rin", u"Rвх", u"Rin", "Ohm", "10k"},
          {"vin", u"Входной сигнал (необязательно)", u"Input signal (optional)", "V", ""}}},
        {"kirchhoff", u"Законы Кирхгофа: две ЭДС и нагрузка", u"Kirchhoff: two sources and a load",
         u"Две ветви с ЭДС и внутренним сопротивлением параллельно нагрузке R3. Метод узловых потенциалов и проверка по первому закону.",
         u"Two branches with an EMF and a resistance in parallel with a load R3. Node voltage method with a check by the current law.",
         u"U = (E1/R1 + E2/R2) / (1/R1 + 1/R2 + 1/R3)   I1 + I2 = I3",
         {{"e1", u"E1", u"E1", "V", "12"}, {"r1", u"R1", u"R1", "Ohm", "2"}, {"e2", u"E2", u"E2", "V", "9"},
          {"r2", u"R2", u"R2", "Ohm", "3"}, {"r3", u"R3 (нагрузка)", u"R3 (load)", "Ohm", "10"}}},
        {"capacitor", u"Конденсатор: заряд и энергия", u"Capacitor charge and energy",
         u"Заряд, энергия и время разряда через резистор до заданного напряжения.",
         u"Charge, energy and discharge time through a resistor to a voltage.",
         u"Q = C·U   W = C·U²/2   t = R·C·ln(U0/U)",
         {{"c", u"Ёмкость", u"Capacitance", "F", "1000u"}, {"u", u"Напряжение", u"Voltage", "V", "12"},
          {"r", u"Резистор разряда (необязательно)", u"Discharge resistor (optional)", "Ohm", ""},
          {"u2", u"До напряжения (необязательно)", u"Down to voltage (optional)", "V", ""}}},
        {"wire", u"Провод: сопротивление и потери", u"Wire resistance and loss",
         u"Медь: ρ = 0,0175, алюминий: 0,028 Ом·мм²/м. Длину считайте туда и обратно.",
         u"Copper: ρ = 0.0175, aluminium: 0.028 Ω·mm²/m. Count the length both ways.",
         u"R = ρ·L/S   ΔU = I·R   P = I²·R",
         {{"l", u"Длина", u"Length", "m", "10"}, {"s", u"Сечение", u"Cross-section", "mm2", "1.5"},
          {"rho", u"Удельное сопротивление", u"Resistivity", "rho", "0.0175"}, {"i", u"Ток (необязательно)", u"Current (optional)", "A", "10"}}},
        {"battery", u"Время работы от батареи", u"Battery life",
         u"Сколько проработает устройство от аккумулятора.", u"How long a device runs on a battery.",
         u"t = C·η / I",
         {{"cap", u"Ёмкость", u"Capacity", "mAh", "2000"}, {"i", u"Потребляемый ток", u"Load current", "A", "150m"},
          {"eff", u"Полезная доля ёмкости", u"Usable share", "%", "80"}}},
        {"db", u"Децибелы", u"Decibels",
         u"Мощность в дБм, отношения мощностей и напряжений в дБ.", u"Power in dBm, power and voltage ratios in dB.",
         u"дБм = 10·lg(P/1 мВт)   дБ = 10·lg(P2/P1) = 20·lg(U2/U1)",
         {{"p", u"Мощность", u"Power", "W", "1m"}, {"pr", u"Отношение мощностей", u"Power ratio", "n", "2"},
          {"vr", u"Отношение напряжений", u"Voltage ratio", "n", "2"}}},
        {"uart", u"UART: делитель скорости", u"UART baud divider",
         u"Регистры BRR (STM32) и UBRR (AVR) и ошибка скорости.", u"BRR (STM32) and UBRR (AVR) registers and the baud error.",
         u"BRR = fclk / baud   UBRR = fclk/(16·baud) − 1",
         {{"fclk", u"Тактовая частота", u"Clock", "Hz", "72M"}, {"baud", u"Скорость, бод", u"Baud rate", "n", "115200"}}},
        {"timer", u"Таймер МК: PSC и ARR", u"MCU timer: PSC and ARR",
         u"Частота переполнения таймера. Оставьте PSC и ARR пустыми и задайте частоту — подберу.",
         u"Timer update frequency. Leave PSC and ARR empty and set a frequency to find them.",
         u"f = fclk / ((PSC + 1)·(ARR + 1))",
         {{"fclk", u"Частота таймера", u"Timer clock", "Hz", "72M"}, {"psc", u"PSC", u"PSC", "n", "7199"},
          {"arr", u"ARR", u"ARR", "n", "9999"}, {"f", u"Нужная частота (необязательно)", u"Wanted frequency (optional)", "Hz", ""}}},
        {"adc", u"АЦП: код ↔ напряжение", u"ADC code ↔ voltage",
         u"Шаг АЦП, напряжение по коду или код по напряжению.", u"ADC step, voltage from a code or a code from a voltage.",
         u"U = код·Uоп / (2ⁿ − 1)",
         {{"bits", u"Разрядность", u"Resolution", "bit", "12"}, {"vref", u"Опорное напряжение", u"Reference", "V", "3.3"},
          {"raw", u"Код", u"Code", "n", "2048"}, {"v", u"Напряжение (необязательно)", u"Voltage (optional)", "V", ""}}},
        {"i2c", u"Подтяжка шины I2C", u"I2C pull-up",
         u"Допустимый диапазон подтягивающих резисторов по ёмкости шины.", u"Allowed pull-up range from the bus capacitance.",
         u"Rmax = tr / (0,8473·Cb)   Rmin = (Vdd − 0,4) / Iol",
         {{"vdd", u"Питание", u"Supply", "V", "3.3"}, {"cb", u"Ёмкость шины", u"Bus capacitance", "F", "100p"},
          {"khz", u"Скорость, кГц (100/400/1000)", u"Speed, kHz (100/400/1000)", "n", "400"}}},
        {"bases", u"Системы счисления и биты", u"Number bases and bits",
         u"Число в hex (0x…), bin (0b…), oct (0o…) или десятичное — во все системы.",
         u"A number in hex (0x…), bin (0b…), oct (0o…) or decimal — in every base.",
         u"0x2A = 0b101010 = 42",
         {{"value", u"Число", u"Number", "text", "0x3F"}}},
        {"kinematics", u"Равноускоренное движение", u"Uniform acceleration",
         u"Скорость и путь через время t.", u"Speed and distance after time t.",
         u"v = v0 + a·t   s = v0·t + a·t²/2",
         {{"v0", u"Начальная скорость", u"Initial speed", "m/s", "0"}, {"a", u"Ускорение", u"Acceleration", "m/s2", "9.81"},
          {"t", u"Время", u"Time", "s", "2"}}},
        {"energy", u"Механическая энергия", u"Mechanical energy",
         u"Кинетическая и потенциальная энергия тела.", u"Kinetic and potential energy of a body.",
         u"Eк = m·v²/2   Eп = m·g·h",
         {{"m", u"Масса", u"Mass", "kg", "1"}, {"v", u"Скорость", u"Speed", "m/s", "10"}, {"h", u"Высота", u"Height", "m", "5"}}},
        {"heat", u"Нагрев: количество теплоты", u"Heating energy",
         u"Теплота на нагрев и время при заданной мощности. Вода: c = 4200.", u"Heat to warm something up and the time at a power. Water: c = 4200.",
         u"Q = c·m·ΔT   t = Q / P",
         {{"m", u"Масса", u"Mass", "kg", "1"}, {"c", u"Удельная теплоёмкость", u"Specific heat", "J/kgK", "4200"},
          {"dt", u"Нагрев на", u"Temperature rise", "K", "80"}, {"p", u"Мощность нагревателя (необязательно)", u"Heater power (optional)", "W", "2k"}}},
        {"coulomb", u"Закон Кулона", u"Coulomb's law",
         u"Сила взаимодействия двух точечных зарядов в вакууме.", u"Force between two point charges in vacuum.",
         u"F = k·|q1·q2| / r²   k = 8,99·10⁹",
         {{"q1", u"Заряд q1", u"Charge q1", "C", "1u"}, {"q2", u"Заряд q2", u"Charge q2", "C", "1u"}, {"r", u"Расстояние", u"Distance", "m", "0.1"}}},
        {"pendulum", u"Математический маятник", u"Simple pendulum",
         u"Период малых колебаний.", u"Period of small oscillations.",
         u"T = 2π·√(L/g)",
         {{"l", u"Длина", u"Length", "m", "1"}, {"g", u"Ускорение свободного падения", u"Gravity", "m/s2", "9.81"}}},
    };
    return list;
}

const Calc *findCalc(const QString &id)
{
    for (const Calc &c : calcs())
        if (QLatin1String(c.id) == id)
            return &c;
    return nullptr;
}

QString tr(Lang lang, const char16_t *ru, const char16_t *en)
{
    return QString::fromUtf16(lang == Lang::Ru ? ru : en);
}

// Standard power ratings a resistor should have for `p` watts (twice the margin).
double ratedPower(double p)
{
    for (double r : {0.125, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0})
        if (p * 2 <= r)
            return r;
    return std::ceil(p * 2);
}

QJsonObject failure(const QString &error) { return QJsonObject{{u"ok"_s, false}, {u"error"_s, error}}; }

} // namespace

// ---- Public helpers ----------------------------------------------------------------

bool Calculators::parseValue(const QString &text, double *value)
{
    return parseQuantity(text, {}, value);
}

QString Calculators::format(double value, const QString &unit, Lang lang, int digits)
{
    if (!std::isfinite(value))
        return u"—"_s;
    const QString name = unitName(unit, lang);
    const auto it = units().find(unit);
    const bool prefixes = it != units().end() ? it->prefixes : false;
    if (!prefixes || value == 0)
        return (number(value, std::max(digits, 3), lang) + (name.isEmpty() ? QString() : u' ' + name)).trimmed();
    static const QList<std::pair<int, std::pair<const char16_t *, const char16_t *>>> table = {
        {-12, {u"п", u"p"}}, {-9, {u"н", u"n"}}, {-6, {u"мк", u"µ"}}, {-3, {u"м", u"m"}}, {0, {u"", u""}},
        {3, {u"к", u"k"}},   {6, {u"М", u"M"}},  {9, {u"Г", u"G"}}};
    int exponent = int(std::floor(std::log10(std::fabs(value)) / 3.0)) * 3;
    exponent = std::clamp(exponent, -12, 9);
    double scaled = value / std::pow(10.0, exponent);
    // 999.96 rounds to 1000: move to the next prefix.
    if (std::fabs(QString::number(scaled, 'g', digits).toDouble()) >= 1000 && exponent < 9) {
        exponent += 3;
        scaled /= 1000;
    }
    QString prefix;
    for (const auto &[e, names] : table)
        if (e == exponent)
            prefix = QString::fromUtf16(lang == Lang::Ru ? names.first : names.second);
    return number(scaled, digits, lang) + u' ' + prefix + name;
}

double Calculators::nearestE(double value, int series, bool up)
{
    static const QList<double> e12 = {1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
    static const QList<double> e24 = {1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
                                      3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1};
    if (!(value > 0) || !std::isfinite(value))
        return kNaN;
    QList<double> base;
    if (series == 96) {
        for (int i = 0; i < 96; ++i)
            base.append(std::round(std::pow(10.0, i / 96.0) * 100) / 100);
    } else {
        base = series == 12 ? e12 : e24;
    }
    const double decade = std::pow(10.0, std::floor(std::log10(value)));
    double best = kNaN;
    for (int d = -1; d <= 1; ++d)
        for (double b : base) {
            const double candidate = b * decade * std::pow(10.0, d);
            if (up && candidate < value * (1 - 1e-9))
                continue;
            if (std::isnan(best) || std::fabs(std::log(candidate / value)) < std::fabs(std::log(best / value)))
                best = candidate;
        }
    return best;
}

// ---- Expression evaluator -----------------------------------------------------------

namespace {

class Parser
{
public:
    explicit Parser(const QString &s)
        : m_s(s)
    {
    }
    bool run(double *value, QString *error)
    {
        *value = expr();
        skip();
        if (m_error.isEmpty() && m_pos < m_s.size())
            m_error = u"unexpected «%1»"_s.arg(m_s.mid(m_pos, 8));
        if (error)
            *error = m_error;
        return m_error.isEmpty() && std::isfinite(*value);
    }

private:
    void skip()
    {
        while (m_pos < m_s.size() && m_s.at(m_pos).isSpace())
            ++m_pos;
    }
    bool eat(QChar c)
    {
        skip();
        if (m_pos < m_s.size() && m_s.at(m_pos) == c) {
            ++m_pos;
            return true;
        }
        return false;
    }
    double expr()
    {
        double v = term();
        for (;;) {
            if (eat(u'+'))
                v += term();
            else if (eat(u'-') || eat(u'−'))
                v -= term();
            else
                return v;
        }
    }
    double term()
    {
        double v = power();
        for (;;) {
            if (eat(u'*') || eat(u'×') || eat(u'·'))
                v *= power();
            else if (eat(u'/') || eat(u':') || eat(u'÷'))
                v /= power();
            else if (startsImplicit())
                v *= power(); // 2pi, 3(4+5)
            else
                return v;
        }
    }
    bool startsImplicit()
    {
        skip();
        return m_pos < m_s.size() && (m_s.at(m_pos) == u'(' || m_s.at(m_pos).isLetter() || m_s.at(m_pos) == u'π');
    }
    double power()
    {
        const double base = unary();
        if (eat(u'^'))
            return std::pow(base, power()); // right-associative
        return base;
    }
    double unary()
    {
        if (eat(u'-') || eat(u'−'))
            return -unary();
        if (eat(u'+'))
            return unary();
        return primary();
    }
    double primary()
    {
        if (++m_depth > 64) {
            m_error = u"too deep"_s;
            return kNaN;
        }
        double v = kNaN;
        skip();
        if (eat(u'(')) {
            v = expr();
            if (!eat(u')'))
                m_error = u"missing )"_s;
        } else if (m_pos < m_s.size() && (m_s.at(m_pos).isDigit() || m_s.at(m_pos) == u'.' || m_s.at(m_pos) == u',')) {
            static const QRegularExpression num(u"\\G(?:\\d+(?:[.,]\\d*)?|[.,]\\d+)(?:[eE][-+]?\\d+)?(мк|[pnuµmkMGпнмкМГ](?![a-zA-Zа-яА-Я]))?"_s);
            const auto m = num.match(m_s, m_pos);
            if (!m.hasMatch()) {
                m_error = u"bad number"_s;
                return kNaN;
            }
            m_pos = m.capturedEnd();
            QString digits = m.captured(0);
            digits.chop(m.captured(1).size());
            v = digits.replace(u',', u'.').toDouble() * prefixFactor(m.captured(1));
        } else if (m_pos < m_s.size() && (m_s.at(m_pos).isLetter() || m_s.at(m_pos) == u'π')) {
            int end = m_pos;
            while (end < m_s.size() && (m_s.at(end).isLetterOrNumber() || m_s.at(end) == u'π'))
                ++end;
            const QString name = m_s.mid(m_pos, end - m_pos).toLower();
            m_pos = end;
            static const QHash<QString, std::function<double(double)>> functions = {
                {u"sqrt"_s, [](double x) { return std::sqrt(x); }}, {u"корень"_s, [](double x) { return std::sqrt(x); }},
                {u"sin"_s, [](double x) { return std::sin(x); }},   {u"cos"_s, [](double x) { return std::cos(x); }},
                {u"tan"_s, [](double x) { return std::tan(x); }},   {u"tg"_s, [](double x) { return std::tan(x); }},
                {u"asin"_s, [](double x) { return std::asin(x); }}, {u"acos"_s, [](double x) { return std::acos(x); }},
                {u"atan"_s, [](double x) { return std::atan(x); }}, {u"log"_s, [](double x) { return std::log10(x); }},
                {u"lg"_s, [](double x) { return std::log10(x); }},  {u"ln"_s, [](double x) { return std::log(x); }},
                {u"exp"_s, [](double x) { return std::exp(x); }},   {u"abs"_s, [](double x) { return std::fabs(x); }}};
            if (name == u"pi" || name == u"π" || name == u"пи")
                v = kPi;
            else if (name == u"e")
                v = std::exp(1.0);
            else if (functions.contains(name)) {
                if (!eat(u'(')) {
                    m_error = u"%1(…)"_s.arg(name);
                    return kNaN;
                }
                v = functions.value(name)(expr());
                if (!eat(u')'))
                    m_error = u"missing )"_s;
            } else {
                m_error = u"unknown «%1»"_s.arg(name);
            }
        } else {
            m_error = u"expected a number"_s;
        }
        --m_depth;
        return v;
    }

    QString m_s;
    int m_pos = 0;
    int m_depth = 0;
    QString m_error;
};

} // namespace

bool Calculators::evaluate(const QString &expression, double *value, QString *error)
{
    if (expression.size() > 300)
        return false;
    Parser parser(expression);
    return parser.run(value, error);
}

// ---- Definitions and computation ----------------------------------------------------

QJsonArray Calculators::definitions(Lang lang)
{
    QJsonArray out;
    for (const Calc &c : calcs())
        out.append(definition(QLatin1String(c.id), lang));
    return out;
}

QJsonObject Calculators::definition(const QString &id, Lang lang)
{
    const Calc *c = findCalc(id);
    if (!c)
        return {};
    QJsonArray inputs;
    for (const Input &i : c->inputs)
        inputs.append(QJsonObject{{u"id"_s, QLatin1String(i.id)}, {u"label"_s, tr(lang, i.ru, i.en)},
                                  {u"unit"_s, unitName(QLatin1String(i.unit), lang)},
                                  {u"text"_s, QLatin1String(i.unit) == u"text"}, {u"value"_s, QLatin1String(i.value)}});
    return QJsonObject{{u"id"_s, id}, {u"name"_s, tr(lang, c->ru, c->en)}, {u"description"_s, tr(lang, c->descRu, c->descEn)},
                       {u"formula"_s, QString::fromUtf16(c->formula)}, {u"inputs"_s, inputs}};
}

QJsonObject Calculators::compute(const QString &id, const QJsonObject &values, Lang lang)
{
    const Calc *c = findCalc(id);
    if (!c)
        return failure(lang == Lang::Ru ? u"Нет такого калькулятора."_s : u"No such calculator."_s);
    const bool ru = lang == Lang::Ru;
    QHash<QString, double> v;
    QHash<QString, QString> raw;
    for (const Input &i : c->inputs) {
        const QString key = QLatin1String(i.id);
        const QString text = values.value(key).toString().trimmed();
        raw.insert(key, text);
        if (QLatin1String(i.unit) == u"text")
            continue;
        double x = kNaN;
        if (!text.isEmpty() && !parseQuantity(text, QLatin1String(i.unit), &x))
            return failure((ru ? u"Не понял значение «%1» в поле «%2»."_s : u"Can't read «%1» in «%2»."_s)
                               .arg(text, tr(lang, i.ru, i.en)));
        v.insert(key, x);
    }
    auto has = [&](const char *key) { return std::isfinite(v.value(QLatin1String(key), kNaN)); };
    auto val = [&](const char *key) { return v.value(QLatin1String(key), kNaN); };
    QJsonArray lines;
    auto line = [&](const char16_t *labelRu, const char16_t *labelEn, const QString &value) {
        lines.append(QJsonObject{{u"label"_s, tr(lang, labelRu, labelEn)}, {u"value"_s, value}});
    };
    auto put = [&](const char16_t *labelRu, const char16_t *labelEn, double value, const char *unit, int digits = 3) {
        line(labelRu, labelEn, format(value, QLatin1String(unit), lang, digits));
    };
    auto need = [&](std::initializer_list<const char *> keys) {
        for (const char *k : keys)
            if (!has(k))
                return false;
        return true;
    };
    const QString missing = ru ? u"Заполните обязательные поля."_s : u"Fill in the required fields."_s;
    const QString impossible = ru ? u"С такими значениями посчитать нельзя."_s : u"These values cannot work."_s;

    if (id == u"ohm") {
        double U = val("u"), I = val("i"), R = val("r"), P = val("p");
        const int known = int(has("u")) + int(has("i")) + int(has("r")) + int(has("p"));
        if (known < 2)
            return failure(ru ? u"Нужны любые две величины из четырёх."_s : u"Two of the four values are needed."_s);
        if (std::isnan(U)) U = !std::isnan(I) && !std::isnan(R) ? I * R : !std::isnan(P) && !std::isnan(I) ? P / I : std::sqrt(P * R);
        if (std::isnan(I)) I = !std::isnan(R) ? U / R : P / U;
        if (std::isnan(R)) R = U / I;
        if (std::isnan(P)) P = U * I;
        if (!std::isfinite(U * I * R * P) && !(U == 0 || I == 0))
            return failure(impossible);
        put(u"Напряжение U", u"Voltage U", U, "V");
        put(u"Ток I", u"Current I", I, "A");
        put(u"Сопротивление R", u"Resistance R", R, "Ohm");
        put(u"Мощность P", u"Power P", P, "W");
        if (std::fabs(U * I - P) > std::fabs(P) * 0.01 + 1e-12 || std::fabs(I * R - U) > std::fabs(U) * 0.01 + 1e-12)
            line(u"Внимание", u"Note", ru ? u"заданные значения противоречат друг другу"_s : u"the given values contradict each other"_s);
    } else if (id == u"led") {
        if (!need({"vs", "vf", "if"}))
            return failure(missing);
        const double n = has("n") ? std::max(1.0, std::round(val("n"))) : 1;
        const double drop = val("vs") - n * val("vf");
        if (drop <= 0 || val("if") <= 0)
            return failure(ru ? u"Напряжения питания не хватает на светодиоды: уменьшите их число или поднимите питание."_s
                              : u"The supply is too low for the LEDs: use fewer in series or a higher supply."_s);
        const double R = drop / val("if");
        const double E = nearestE(R, 24, true);
        const double I = drop / E;
        put(u"Расчётное сопротивление", u"Exact resistance", R, "Ohm");
        put(u"Ближайший из ряда E24 (вверх)", u"Nearest E24 (rounded up)", E, "Ohm");
        put(u"Ток с этим резистором", u"Current with it", I, "A");
        put(u"Мощность на резисторе", u"Resistor dissipation", I * drop, "W");
        put(u"Резистор с запасом, не меньше", u"Resistor rating, at least", ratedPower(I * drop), "W");
    } else if (id == u"divider") {
        if (!has("vin"))
            return failure(missing);
        double R1 = val("r1"), R2 = val("r2");
        const double Vin = val("vin");
        if (has("vout") && (std::isnan(R1) != std::isnan(R2))) {
            const double k = val("vout") / Vin;
            if (k <= 0 || k >= 1)
                return failure(impossible);
            if (std::isnan(R1)) {
                R1 = R2 * (1 - k) / k;
                put(u"Нужный R1", u"R1 needed", R1, "Ohm");
                put(u"Ближайший E24", u"Nearest E24", R1 = nearestE(R1), "Ohm");
            } else {
                R2 = R1 * k / (1 - k);
                put(u"Нужный R2", u"R2 needed", R2, "Ohm");
                put(u"Ближайший E24", u"Nearest E24", R2 = nearestE(R2), "Ohm");
            }
        }
        if (std::isnan(R1) || std::isnan(R2) || R1 + R2 <= 0)
            return failure(missing);
        const double Vout = Vin * R2 / (R1 + R2);
        put(u"Выходное напряжение", u"Output voltage", Vout, "V");
        put(u"Коэффициент деления", u"Ratio", R2 / (R1 + R2), "n");
        put(u"Ток делителя", u"Divider current", Vin / (R1 + R2), "A");
        put(u"Рассеиваемая мощность", u"Dissipated power", Vin * Vin / (R1 + R2), "W");
        put(u"Выходное сопротивление (Rth = R1‖R2)", u"Output resistance (Rth = R1‖R2)", R1 * R2 / (R1 + R2), "Ohm");
    } else if (id == u"series") {
        QList<double> list;
        for (const QString &part : raw.value(u"values"_s).split(rx(u"[;,\\s]+(?=[-+\\d.])"_s), Qt::SkipEmptyParts)) {
            double x = kNaN;
            if (!parseQuantity(part.trimmed(), {}, &x) || x <= 0)
                return failure((ru ? u"Не понял номинал «%1»."_s : u"Can't read «%1»."_s).arg(part.trimmed()));
            list.append(x);
        }
        if (list.size() < 2)
            return failure(ru ? u"Нужно хотя бы два номинала."_s : u"At least two values are needed."_s);
        double sum = 0, inverse = 0;
        for (double x : list) {
            sum += x;
            inverse += 1 / x;
        }
        put(u"Сумма — R и L последовательно, C параллельно", u"Sum — R and L in series, C in parallel", sum, "");
        put(u"1/Σ(1/x) — R и L параллельно, C последовательно", u"1/Σ(1/x) — R and L in parallel, C in series", 1 / inverse, "");
    } else if (id == u"rc") {
        if (!need({"r", "c"}))
            return failure(missing);
        const double tau = val("r") * val("c");
        put(u"Постоянная времени τ", u"Time constant τ", tau, "s");
        put(u"Частота среза (−3 дБ)", u"Cutoff frequency (−3 dB)", 1 / (2 * kPi * tau), "Hz");
        put(u"Заряд до 63 % (1τ)", u"Charge to 63% (1τ)", tau, "s");
        put(u"Заряд до 95 % (3τ)", u"Charge to 95% (3τ)", 3 * tau, "s");
        put(u"Заряд до 99 % (5τ)", u"Charge to 99% (5τ)", 5 * tau, "s");
    } else if (id == u"reactance") {
        if (!has("c") && !has("l"))
            return failure(missing);
        if (has("f") && has("c"))
            put(u"Xc на этой частоте", u"Xc at this frequency", 1 / (2 * kPi * val("f") * val("c")), "Ohm");
        if (has("f") && has("l"))
            put(u"XL на этой частоте", u"XL at this frequency", 2 * kPi * val("f") * val("l"), "Ohm");
        if (has("c") && has("l")) {
            put(u"Резонансная частота f0", u"Resonant frequency f0", 1 / (2 * kPi * std::sqrt(val("l") * val("c"))), "Hz");
            put(u"Волновое сопротивление √(L/C)", u"Characteristic impedance √(L/C)", std::sqrt(val("l") / val("c")), "Ohm");
        }
    } else if (id == u"color") {
        const QString text = raw.value(u"value"_s);
        QList<int> found;
        for (const QString &w : text.split(rx(u"[\\s,;—–-]+"_s), Qt::SkipEmptyParts))
            if (const int i = colourIndex(w); i >= 0)
                found.append(i);
        if (found.size() >= 3) {
            // 3–4 bands: two digits; 5–6: three digits. The band after the multiplier is the tolerance.
            const int digits = found.size() >= 5 ? 3 : 2;
            double value = 0;
            for (int k = 0; k < digits; ++k) {
                if (found.at(k) > 9)
                    return failure(ru ? u"Золотой и серебряный не бывают цифрой."_s : u"Gold and silver are never a digit."_s);
                value = value * 10 + found.at(k);
            }
            const int mult = found.at(digits);
            value *= mult == 10 ? 0.1 : mult == 11 ? 0.01 : std::pow(10.0, mult);
            put(u"Номинал", u"Value", value, "Ohm");
            if (found.size() > digits + 1 && colours().at(found.at(digits + 1)).tolerance > 0)
                line(u"Допуск", u"Tolerance", u"±"_s + number(colours().at(found.at(digits + 1)).tolerance, 3, lang) + u" %"_s);
            else if (found.size() == digits + 1)
                line(u"Допуск", u"Tolerance", u"±20 %"_s);
        } else {
            double value = kNaN;
            if (!parseQuantity(text, u"Ohm"_s, &value) || value <= 0)
                return failure(ru ? u"Введите номинал (4.7k) или цвета полос через пробел."_s
                                  : u"Enter a value (4.7k) or band colours separated by spaces."_s);
            line(u"4 полосы (±5 %: + золотой)", u"4 bands (±5%: + gold)", bands(value, 2, lang));
            line(u"5 полос (±1 %: + коричневый)", u"5 bands (±1%: + brown)", bands(value, 3, lang));
            const double e = nearestE(value, 24);
            if (std::fabs(e - value) > value * 1e-6)
                put(u"Ближайший в ряду E24", u"Nearest E24 value", e, "Ohm");
        }
    } else if (id == u"timer555") {
        if (!need({"r1", "r2", "c"}))
            return failure(missing);
        const double R1 = val("r1"), R2 = val("r2"), C = val("c");
        const double th = 0.693 * (R1 + R2) * C, tl = 0.693 * R2 * C;
        put(u"Частота", u"Frequency", 1 / (th + tl), "Hz");
        put(u"Время высокого уровня", u"High time", th, "s");
        put(u"Время низкого уровня", u"Low time", tl, "s");
        put(u"Коэффициент заполнения", u"Duty cycle", 100 * th / (th + tl), "%");
    } else if (id == u"opamp") {
        if (!need({"rf", "rin"}))
            return failure(missing);
        const double k = val("rf") / val("rin");
        put(u"Инвертирующий: K", u"Inverting gain", -k, "n");
        put(u"Неинвертирующий: K", u"Non-inverting gain", 1 + k, "n");
        put(u"Неинвертирующий, дБ", u"Non-inverting, dB", 20 * std::log10(1 + k), "dB");
        if (has("vin")) {
            put(u"Выход инвертирующего", u"Inverting output", -k * val("vin"), "V");
            put(u"Выход неинвертирующего", u"Non-inverting output", (1 + k) * val("vin"), "V");
        }
    } else if (id == u"kirchhoff") {
        if (!need({"e1", "r1", "e2", "r2", "r3"}))
            return failure(missing);
        const double E1 = val("e1"), R1 = val("r1"), E2 = val("e2"), R2 = val("r2"), R3 = val("r3");
        if (R1 <= 0 || R2 <= 0 || R3 <= 0)
            return failure(impossible);
        const double U = (E1 / R1 + E2 / R2) / (1 / R1 + 1 / R2 + 1 / R3);
        const double I1 = (E1 - U) / R1, I2 = (E2 - U) / R2, I3 = U / R3;
        put(u"Напряжение на нагрузке", u"Load voltage", U, "V");
        put(u"I1 (из ветви E1)", u"I1 (from E1)", I1, "A");
        put(u"I2 (из ветви E2)", u"I2 (from E2)", I2, "A");
        put(u"I3 (через нагрузку)", u"I3 (through the load)", I3, "A");
        line(u"Первый закон: I1 + I2 − I3", u"Current law: I1 + I2 − I3", format(I1 + I2 - I3, u"A"_s, lang) + u" ≈ 0"_s);
        if (I1 < 0 || I2 < 0)
            line(u"Заметьте", u"Note",
                 ru ? u"отрицательный ток — источник заряжается (ток идёт навстречу ЭДС)"_s
                    : u"a negative current means that source is being charged (current flows against its EMF)"_s);
        put(u"Мощность в нагрузке", u"Load power", U * I3, "W");
    } else if (id == u"capacitor") {
        if (!need({"c", "u"}))
            return failure(missing);
        put(u"Заряд Q", u"Charge Q", val("c") * val("u"), "C");
        put(u"Энергия W", u"Energy W", val("c") * val("u") * val("u") / 2, "J");
        if (has("r")) {
            const double target = has("u2") ? val("u2") : val("u") * 0.05;
            if (target <= 0 || target >= val("u"))
                return failure(impossible);
            put(u"Время разряда до заданного напряжения", u"Discharge time to that voltage",
                val("r") * val("c") * std::log(val("u") / target), "s");
            put(u"Начальный ток разряда", u"Initial discharge current", val("u") / val("r"), "A");
        }
    } else if (id == u"wire") {
        if (!need({"l", "s", "rho"}) || val("s") <= 0)
            return failure(missing);
        const double R = val("rho") * val("l") / val("s");
        put(u"Сопротивление провода", u"Wire resistance", R, "Ohm");
        if (has("i")) {
            put(u"Падение напряжения", u"Voltage drop", val("i") * R, "V");
            put(u"Потери мощности", u"Power loss", val("i") * val("i") * R, "W");
            put(u"Плотность тока", u"Current density", val("i") / val("s"), "n");
        }
    } else if (id == u"battery") {
        if (!need({"cap", "i"}) || val("i") <= 0)
            return failure(missing);
        const double share = has("eff") ? val("eff") / 100 : 0.8;
        const double hours = val("cap") * share / (val("i") * 1000);
        put(u"Время работы", u"Run time", hours, "h");
        if (hours > 48)
            put(u"В сутках", u"In days", hours / 24, "n");
    } else if (id == u"db") {
        if (!has("p") && !has("pr") && !has("vr"))
            return failure(missing);
        if (has("p") && val("p") > 0)
            put(u"Мощность в дБм", u"Power in dBm", 10 * std::log10(val("p") / 1e-3), "dBm");
        if (has("pr") && val("pr") > 0)
            put(u"Отношение мощностей", u"Power ratio", 10 * std::log10(val("pr")), "dB");
        if (has("vr") && val("vr") > 0)
            put(u"Отношение напряжений", u"Voltage ratio", 20 * std::log10(val("vr")), "dB");
    } else if (id == u"uart") {
        if (!need({"fclk", "baud"}) || val("baud") <= 0)
            return failure(missing);
        const double f = val("fclk"), b = val("baud");
        const long long brr = std::llround(f / b);
        put(u"STM32 BRR (oversampling 16)", u"STM32 BRR (oversampling 16)", double(brr), "n");
        line(u"BRR в hex", u"BRR in hex", u"0x"_s + QString::number(brr, 16).toUpper());
        put(u"Ошибка скорости STM32", u"STM32 baud error", 100 * (f / brr - b) / b, "%");
        const long long ubrr = std::llround(f / (16 * b)) - 1;
        if (ubrr >= 0) {
            put(u"AVR UBRR (U2X = 0)", u"AVR UBRR (U2X = 0)", double(ubrr), "n");
            put(u"Ошибка скорости AVR", u"AVR baud error", 100 * (f / (16.0 * (ubrr + 1)) - b) / b, "%");
        }
        const long long ubrr2 = std::llround(f / (8 * b)) - 1;
        if (ubrr2 >= 0) {
            put(u"AVR UBRR (U2X = 1)", u"AVR UBRR (U2X = 1)", double(ubrr2), "n");
            put(u"Ошибка с U2X = 1", u"Error with U2X = 1", 100 * (f / (8.0 * (ubrr2 + 1)) - b) / b, "%");
        }
        line(u"Норма", u"Rule of thumb", ru ? u"ошибка до ±2 % обычно работает, лучше < 1 %"_s : u"up to ±2% usually works, < 1% is better"_s);
    } else if (id == u"timer") {
        if (!has("fclk"))
            return failure(missing);
        const double f = val("fclk");
        double psc = val("psc"), arr = val("arr");
        if ((std::isnan(psc) || std::isnan(arr)) && has("f")) {
            const double total = f / val("f");
            if (total < 1)
                return failure(impossible);
            psc = std::ceil(total / 65536.0) - 1;
            if (psc > 65535)
                return failure(ru ? u"Частота слишком низкая для 16-битного таймера."_s : u"Too slow for a 16-bit timer."_s);
            arr = std::round(total / (psc + 1)) - 1;
            put(u"PSC", u"PSC", psc, "n");
            put(u"ARR", u"ARR", arr, "n");
        }
        if (std::isnan(psc) || std::isnan(arr))
            return failure(missing);
        const double update = f / ((psc + 1) * (arr + 1));
        put(u"Частота переполнения", u"Update frequency", update, "Hz", 6);
        put(u"Период", u"Period", 1 / update, "s", 6);
        put(u"Тик таймера", u"Timer tick", (psc + 1) / f, "s");
        if (has("f"))
            put(u"Отклонение от нужной", u"Off the wanted one by", 100 * (update - val("f")) / val("f"), "%");
    } else if (id == u"adc") {
        if (!need({"bits", "vref"}))
            return failure(missing);
        const double top = std::pow(2.0, std::round(val("bits"))) - 1;
        put(u"Шаг (1 LSB)", u"Step (1 LSB)", val("vref") / (top + 1), "V");
        if (has("raw"))
            put(u"Напряжение для кода", u"Voltage for the code", val("raw") * val("vref") / top, "V");
        if (has("v"))
            put(u"Код для напряжения", u"Code for the voltage", std::round(std::clamp(val("v") / val("vref"), 0.0, 1.0) * top), "n");
    } else if (id == u"i2c") {
        if (!need({"vdd", "cb"}))
            return failure(missing);
        const double khz = has("khz") ? val("khz") : 100;
        const double tr = khz > 400 ? 120e-9 : khz > 100 ? 300e-9 : 1000e-9;
        const double iol = khz > 400 ? 20e-3 : 3e-3;
        const double rmax = tr / (0.8473 * val("cb")), rmin = (val("vdd") - 0.4) / iol;
        put(u"Rmin (по току выхода)", u"Rmin (output current)", rmin, "Ohm");
        put(u"Rmax (по времени фронта)", u"Rmax (rise time)", rmax, "Ohm");
        if (rmax < rmin)
            line(u"Внимание", u"Note", ru ? u"шина слишком ёмкая для этой скорости: снизьте скорость или ёмкость"_s
                                         : u"the bus is too capacitive for this speed: lower the speed or the capacitance"_s);
        else
            put(u"Подойдёт (E12)", u"A good value (E12)", nearestE(std::sqrt(rmin * rmax), 12), "Ohm");
    } else if (id == u"bases") {
        QString t = raw.value(u"value"_s).toLower().remove(u' ').remove(u'_').remove(u'\'');
        int base = 10;
        if (t.startsWith(u"0x")) { base = 16; t = t.mid(2); }
        else if (t.startsWith(u"0b")) { base = 2; t = t.mid(2); }
        else if (t.startsWith(u"0o")) { base = 8; t = t.mid(2); }
        else if (t.endsWith(u'h') && t.size() > 1) { base = 16; t.chop(1); }
        bool ok = false;
        const qulonglong n = t.toULongLong(&ok, base);
        if (!ok)
            return failure(ru ? u"Введите число: 42, 0x2A, 0b101010 или 0o52."_s : u"Enter a number: 42, 0x2A, 0b101010 or 0o52."_s);
        QString bin = QString::number(n, 2);
        bin = bin.rightJustified(((bin.size() + 3) / 4) * 4, u'0');
        for (int i = bin.size() - 4; i > 0; i -= 4)
            bin.insert(i, u' ');
        line(u"Десятичное", u"Decimal", QString::number(n));
        line(u"Шестнадцатеричное", u"Hexadecimal", u"0x"_s + QString::number(n, 16).toUpper());
        line(u"Двоичное", u"Binary", u"0b "_s + bin);
        line(u"Восьмеричное", u"Octal", u"0o"_s + QString::number(n, 8));
        line(u"Установленных битов", u"Bits set", QString::number(qPopulationCount(quint64(n))));
        line(u"Нужно бит", u"Bits needed", QString::number(n ? 64 - qCountLeadingZeroBits(quint64(n)) : 1));
        if (n >= 32 && n < 127)
            line(u"Символ ASCII", u"ASCII character", u"'"_s + QChar(char16_t(n)) + u"'"_s);
    } else if (id == u"kinematics") {
        if (!need({"a", "t"}))
            return failure(missing);
        const double v0 = has("v0") ? val("v0") : 0;
        put(u"Скорость v", u"Speed v", v0 + val("a") * val("t"), "m/s");
        put(u"Путь s", u"Distance s", v0 * val("t") + val("a") * val("t") * val("t") / 2, "m");
    } else if (id == u"energy") {
        if (!has("m"))
            return failure(missing);
        double total = 0;
        if (has("v")) {
            const double ek = val("m") * val("v") * val("v") / 2;
            total += ek;
            put(u"Кинетическая Eк", u"Kinetic Ek", ek, "J");
        }
        if (has("h")) {
            const double ep = val("m") * 9.81 * val("h");
            total += ep;
            put(u"Потенциальная Eп", u"Potential Ep", ep, "J");
            put(u"Скорость при падении с h", u"Speed after falling h", std::sqrt(2 * 9.81 * val("h")), "m/s");
        }
        put(u"Полная механическая", u"Total mechanical", total, "J");
    } else if (id == u"heat") {
        if (!need({"m", "c", "dt"}))
            return failure(missing);
        const double q = val("c") * val("m") * val("dt");
        put(u"Количество теплоты Q", u"Heat Q", q, "J");
        put(u"В киловатт-часах", u"In kilowatt-hours", q / 3.6e6, "kWh");
        if (has("p") && val("p") > 0)
            put(u"Время нагрева (без потерь)", u"Heating time (no losses)", q / val("p"), "s");
    } else if (id == u"coulomb") {
        if (!need({"q1", "q2", "r"}) || val("r") <= 0)
            return failure(missing);
        put(u"Сила F", u"Force F", 8.9875e9 * std::fabs(val("q1") * val("q2")) / (val("r") * val("r")), "N");
        line(u"Характер", u"Kind", val("q1") * val("q2") > 0 ? (ru ? u"отталкивание"_s : u"repulsion"_s)
                                                            : (ru ? u"притяжение"_s : u"attraction"_s));
    } else if (id == u"pendulum") {
        if (!has("l") || val("l") <= 0)
            return failure(missing);
        const double g = has("g") ? val("g") : 9.81;
        const double T = 2 * kPi * std::sqrt(val("l") / g);
        put(u"Период T", u"Period T", T, "s");
        put(u"Частота", u"Frequency", 1 / T, "Hz");
    }
    if (lines.isEmpty())
        return failure(missing);
    return QJsonObject{{u"ok"_s, true}, {u"lines"_s, lines}, {u"formula"_s, QString::fromUtf16(c->formula)}};
}

// ---- Chat ------------------------------------------------------------------------------

namespace {

struct Quantity
{
    double value;
    QString unit; // V, A, Ohm, W, F, H, Hz, s, mAh or "" (a bare number, maybe with a prefix)
    bool prefixed;
};

QList<Quantity> quantities(const QString &text)
{
    static const QRegularExpression token(
        u"(?<![\\p{L}\\d.,])([-+]?\\d+(?:[.,]\\d+)?|\\d+(?:мк|[pnuµkKMRrпнмкКМ])\\d+)\\s?(мк|[pnuµμmkKMGпнмкКМГ])?\\s?"
        u"(мА·ч|мАч|mAh|Ач|Ah|Ом|ом|Ω|ohms?|Ohms?|Вт|вт|Гн|гн|Гц|гц|Hz|hz|Ф|ф|F|В|в|V|v|А|а|A|W|H|с|s)?(?![\\p{L}\\d])"_s);
    QList<Quantity> out;
    auto it = token.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        double value = kNaN;
        QString number = m.captured(1);
        const QString prefix = m.captured(2);
        QString unit = m.captured(3);
        if (!parseQuantity(number + prefix, {}, &value))
            continue;
        const QString u = unit.toLower();
        QString kind;
        if (u.startsWith(u"ма") || u == u"mah")
            kind = u"mAh"_s;
        else if (u == u"ач" || u == u"ah") {
            kind = u"mAh"_s;
            value *= 1000;
        } else if (u == u"ом" || u == u"ω" || u.startsWith(u"ohm"))
            kind = u"Ohm"_s;
        else if (u == u"вт" || unit == u"W")
            kind = u"W"_s;
        else if (u == u"гн" || unit == u"H")
            kind = u"H"_s;
        else if (u == u"гц" || u == u"hz")
            kind = u"Hz"_s;
        else if (u == u"ф" || unit == u"F")
            kind = u"F"_s;
        else if (u == u"в" || u == u"v")
            kind = u"V"_s;
        else if (u == u"а" || unit == u"A")
            kind = u"A"_s;
        else if (u == u"с" || u == u"s")
            kind = u"s"_s;
        out.append({value, kind, !prefix.isEmpty() || number.contains(rx(u"[^\\d.,+-]"_s))});
    }
    return out;
}

QList<double> ofKind(const QList<Quantity> &list, const QString &kind)
{
    QList<double> out;
    for (const Quantity &q : list)
        if (q.unit == kind)
            out.append(q.value);
    return out;
}

// Resistances: values in ohms, or bare numbers with a prefix ("10к", "4k7").
QList<double> resistances(const QList<Quantity> &list)
{
    QList<double> out;
    for (const Quantity &q : list)
        if (q.unit == u"Ohm" || (q.unit.isEmpty() && q.prefixed))
            out.append(q.value);
    return out;
}

QString render(const QString &title, const QJsonObject &result, Lang lang)
{
    if (!result.value(u"ok"_s).toBool())
        return title + u"\n"_s + result.value(u"error"_s).toString();
    QString out = u"**"_s + title + u"**\n"_s;
    for (const auto &v : result.value(u"lines"_s).toArray())
        out += u"- "_s + v.toObject().value(u"label"_s).toString() + u": **"_s + v.toObject().value(u"value"_s).toString() + u"**\n"_s;
    out += u"\n`"_s + result.value(u"formula"_s).toString() + u"`"_s;
    out += lang == Lang::Ru ? u"\n\nПосчитано на этом компьютере. Калькуляторы — в «Центр управления → Навыки»."_s
                            : u"\n\nCalculated on this computer. Calculators: Control center → Skills."_s;
    return out;
}

QString v(double x) { return QString::number(x, 'g', 12); }

} // namespace

bool Calculators::answer(const QString &input, Lang lang, QString *reply)
{
    const QString text = input.trimmed();
    if (text.isEmpty() || text.size() > 200)
        return false;
    const QString lower = text.toLower().replace(u'ё', u'е');
    auto run = [&](const char *id, const QJsonObject &values) {
        *reply = render(definition(QLatin1String(id), lang).value(u"name"_s).toString(),
                        compute(QLatin1String(id), values, lang), lang);
        return true;
    };

    // "посчитай 2*pi*50*100n", or a bare "12/(4.7+10)".
    static const QRegularExpression calc = rx(
        u"^(?:посчитай|вычисли|сколько будет|calc(?:ulate)?|compute|evaluate)\\s*[:,]?\\s*(.+?)\\s*[=?]?$"_s);
    static const QRegularExpression bare(u"^[-+(]*\\d[\\d\\s.,+\\-*/^()×·÷πa-zA-Zа-яА-Я]*$"_s);
    static const QRegularExpression op(u"\\d\\s*[-+*/^×·÷]\\s*[\\d(a-zπ]"_s);
    QString expression;
    if (const auto m = calc.match(text); m.hasMatch())
        expression = m.captured(1);
    else if (bare.match(text).hasMatch() && op.match(text).hasMatch() && !text.contains(rx(u"\\p{L}{4,}"_s)))
        expression = text;
    if (!expression.isEmpty()) {
        expression.remove(rx(u"\\s*=\\s*$"_s));
        double value = 0;
        if (evaluate(expression, &value)) {
            *reply = u"`"_s + expression.simplified() + u" = "_s + number(value, 10, lang) + u"`"_s;
            if (std::fabs(value) >= 1e3 || (std::fabs(value) < 1e-2 && value != 0))
                *reply += u"  ("_s + format(value, u""_s, lang, 4) + u")"_s;
            return true;
        }
        return false;
    }

    // "0x3F в двоичной", "255 в hex", "переведи 0b1010".
    static const QRegularExpression base = rx(
        u"^(?:переведи|convert)?\\s*(0x[0-9a-f_]+|0b[01_]+|0o[0-7_]+|\\d+)\\s*(?:в|to|in)\\s*(?:hex|bin|oct|dec|шестнадцат\\w*|двоичн\\w*|восьмеричн\\w*|десятичн\\w*)(?:\\s+\\S+)?\\s*\\??$|"
        u"^(?:hex|bin|dec|oct)\\s+(0x[0-9a-f_]+|0b[01_]+|0o[0-7_]+|\\d+)\\s*$"_s);
    if (const auto m = base.match(text); m.hasMatch())
        return run("bases", {{u"value"_s, m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)}});

    const QList<Quantity> q = quantities(text);
    const QList<double> volts = ofKind(q, u"V"_s), amps = ofKind(q, u"A"_s), watts = ofKind(q, u"W"_s);
    const QList<double> farads = ofKind(q, u"F"_s), henries = ofKind(q, u"H"_s), hertz = ofKind(q, u"Hz"_s);
    const QList<double> ohms = resistances(q);
    auto has = [&](const char16_t *pattern) { return lower.contains(rx(QString::fromUtf16(pattern))); };

    // Colours of a resistor: three or more colour words.
    int colourWords = 0;
    for (const QString &w : lower.split(rx(u"[\\s,;—–-]+"_s), Qt::SkipEmptyParts))
        colourWords += colourIndex(w) >= 0 && w.size() >= 3;
    if (colourWords >= 3 && has(u"резист|полос|band|colou?r|цвет|маркир|номинал|\\b(?:черн|коричн|красн)"))
        return run("color", {{u"value"_s, text}});
    if (has(u"маркировк|цвет\\w* полос|colou?r code|цвета резистора") && ohms.size() == 1)
        return run("color", {{u"value"_s, v(ohms.first())}});

    if (has(u"светодиод|\\bled\\b") && !volts.isEmpty()) {
        QJsonObject in{{u"vs"_s, v(volts.at(0))}, {u"vf"_s, volts.size() > 1 ? v(volts.at(1)) : (has(u"син|бел|blue|white|зел.н.*ярк") ? u"3.1"_s : u"2"_s)},
                       {u"if"_s, amps.isEmpty() ? u"0.02"_s : v(amps.first())}};
        return run("led", in);
    }
    if (has(u"делител|divider") && volts.size() == 1 && ohms.size() == 2)
        return run("divider", {{u"vin"_s, v(volts.first())}, {u"r1"_s, v(ohms.at(0))}, {u"r2"_s, v(ohms.at(1))}});
    if (has(u"параллельн|последовательн|parallel|series")) {
        QStringList values;
        for (const Quantity &x : q)
            if (x.unit == u"Ohm" || x.unit == u"F" || x.unit == u"H" || (x.unit.isEmpty() && x.prefixed))
                values.append(v(x.value));
        if (values.size() >= 2)
            return run("series", {{u"values"_s, values.join(u", "_s)}});
    }
    if (has(u"\\b555\\b") && ohms.size() >= 2 && !farads.isEmpty())
        return run("timer555", {{u"r1"_s, v(ohms.at(0))}, {u"r2"_s, v(ohms.at(1))}, {u"c"_s, v(farads.first())}});
    if (has(u"резонанс|resonan|колебательн|\\blc\\b") && !henries.isEmpty() && !farads.isEmpty())
        return run("reactance", {{u"l"_s, v(henries.first())}, {u"c"_s, v(farads.first())}});
    if (has(u"реактивн|reactance|импеданс") && !hertz.isEmpty() && (!farads.isEmpty() || !henries.isEmpty()))
        return run("reactance", {{u"f"_s, v(hertz.first())}, {u"c"_s, farads.isEmpty() ? QString() : v(farads.first())},
                                 {u"l"_s, henries.isEmpty() ? QString() : v(henries.first())}});
    if (has(u"\\brc\\b|постоянн\\w* времени|частот\\w* среза|cutoff|time constant|rc[- ]?фильтр") && ohms.size() == 1
        && farads.size() == 1)
        return run("rc", {{u"r"_s, v(ohms.first())}, {u"c"_s, v(farads.first())}});
    if (has(u"аккумулятор|батаре|battery") && !ofKind(q, u"mAh"_s).isEmpty() && !amps.isEmpty())
        return run("battery", {{u"cap"_s, v(ofKind(q, u"mAh"_s).first())}, {u"i"_s, v(amps.first())}});
    if (has(u"\\buart\\b|\\bbaud|\\bбод|\\bbrr\\b|\\bubrr\\b") && !hertz.isEmpty()) {
        for (const Quantity &x : q)
            if (x.unit.isEmpty() && !x.prefixed && x.value >= 300 && x.value <= 1e7)
                return run("uart", {{u"fclk"_s, v(hertz.first())}, {u"baud"_s, v(x.value)}});
    }
    // Ohm's law: "U=5 R=220", or two of voltage/current/resistance/power with a word about them.
    static const QRegularExpression assign = rx(u"\\b([uirp])\\s*=\\s*([-+]?[\\d.,]+\\s*(?:мк|[pnuµmkKMGпнмкКМГ])?)"_s);
    QJsonObject ohm;
    auto it = assign.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        ohm[m.captured(1).toLower()] = m.captured(2);
    }
    if (ohm.size() >= 2)
        return run("ohm", ohm);
    const int kinds = int(!volts.isEmpty()) + int(!amps.isEmpty()) + int(!ohms.isEmpty()) + int(!watts.isEmpty());
    if (kinds >= 2 && q.size() == kinds && has(u"закон\\w* ома|ohm|\\bток|напряжени|сопротивлени|мощност|current|voltage|resistance|power|сколько")) {
        if (!volts.isEmpty()) ohm[u"u"_s] = v(volts.first());
        if (!amps.isEmpty()) ohm[u"i"_s] = v(amps.first());
        if (!ohms.isEmpty()) ohm[u"r"_s] = v(ohms.first());
        if (!watts.isEmpty()) ohm[u"p"_s] = v(watts.first());
        return run("ohm", ohm);
    }
    return false;
}

} // namespace jarvis
