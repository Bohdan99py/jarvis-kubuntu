#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "language.h"

namespace jarvis {

// Offline engineering calculators: Ohm's law, LED resistor, divider, RC,
// reactance, 555, op-amp, UART, timers, physics and more, plus a safe
// arithmetic evaluator. Values accept SI prefixes in Russian or English and
// resistor notation: "4.7k", "4k7", "100n", "2,2 мкФ", "12В", "20mA".
// Skills list the calculators they bring (the "tools" field).
class Calculators
{
public:
    // [{id, name, description, formula, inputs: [{id, label, unit, value, hint}]}] in `lang`.
    static QJsonArray definitions(Lang lang);
    static QJsonObject definition(const QString &id, Lang lang);
    // values: {inputId: "10k", …}; empty inputs count as unknown where a
    // calculator can solve for them. Returns {ok, lines: [{label, value}], error}.
    static QJsonObject compute(const QString &id, const QJsonObject &values, Lang lang);

    // A calculation asked in chat ("делитель 12 В 10к 4,7к", "U=5 R=220",
    // "посчитай 2*pi*50"). True with the answer when the text is one.
    static bool answer(const QString &text, Lang lang, QString *reply);

    // "4.7k" -> 4700. Units after the prefix are allowed and ignored.
    static bool parseValue(const QString &text, double *value);
    // 4700 -> "4,7 кОм" / "4.7 kΩ" for the unit key ("Ohm", "V", "F", …).
    static QString format(double value, const QString &unit, Lang lang, int digits = 3);
    // Nearest value of the E12/E24/E96 series (`up`: never below `value`).
    static double nearestE(double value, int series = 24, bool up = false);
    // Arithmetic with + - * / ^ ( ), pi, e, sqrt, sin, cos, tan, log, ln, abs and SI suffixes.
    static bool evaluate(const QString &expression, double *value, QString *error = nullptr);
};

} // namespace jarvis
