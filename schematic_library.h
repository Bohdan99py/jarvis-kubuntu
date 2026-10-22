#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "language.h"

namespace jarvis {

// Example circuits of the Electronics skill (schematics/examples.json, drawn
// by schematics/generate.py): a picture, the parts, what every net connects,
// formulas and, where it can be simulated, a SPICE deck. They can be shown,
// explained in chat, saved for ngspice or imported into KiCad Pcbnew.
class SchematicLibrary
{
public:
    // [{id, title, description, category, tags, image, parts, nets, formulas, spice}], texts in `lang`.
    static QJsonArray list(Lang lang);
    static QJsonObject example(const QString &id, Lang lang);

    // "Parts … Nets …" in the form Jarvis gives Claude for attached schematics.
    static QString digest(const QJsonObject &example);
    // A KiCad netlist (kicadsexpr) of the example: Pcbnew → File → Import → Netlist.
    static QString kicadNetlist(const QJsonObject &example);
    // A bill of materials, one row per value and footprint:
    // "Qty;References;Value;Footprint;Symbol". `parts`: [{ref, value, footprint, symbol}].
    static QString bomCsv(const QJsonArray &parts);

    // "пример схемы на 555", "покажи схему делителя", "example circuit LED":
    // true with the example (or the list of them) as the answer.
    static bool answer(const QString &text, Lang lang, QString *reply);
};

} // namespace jarvis
