#include "schematic_library.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include "memory_text.h"

static void initSchematics() { Q_INIT_RESOURCE(schematics); }

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

const QJsonArray &raw()
{
    static const QJsonArray examples = [] {
        initSchematics();
        QFile f(u":/schematics/examples.json"_s);
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().value(u"examples"_s).toArray()
                                           : QJsonArray{};
    }();
    return examples;
}

QJsonObject localized(QJsonObject e, Lang lang)
{
    for (const QString &field : {u"title"_s, u"description"_s}) {
        const QString en = e.value(field + u"_en"_s).toString();
        if (lang == Lang::En && !en.isEmpty())
            e[field] = en;
        e.remove(field + u"_en"_s);
    }
    e[u"image"_s] = u"qrc:/schematics/"_s + e.value(u"id"_s).toString() + u".svg"_s;
    return e;
}

QString quoted(const QString &s)
{
    QString out = s;
    out.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);
    return u'"' + out + u'"';
}

// "Device:R" -> {"Device", "R"}.
std::pair<QString, QString> libPart(const QString &symbol)
{
    const qsizetype colon = symbol.indexOf(u':');
    return colon < 0 ? std::pair{QString(), symbol} : std::pair{symbol.left(colon), symbol.mid(colon + 1)};
}

} // namespace

QJsonArray SchematicLibrary::list(Lang lang)
{
    QJsonArray out;
    for (const auto &v : raw())
        out.append(localized(v.toObject(), lang));
    return out;
}

QJsonObject SchematicLibrary::example(const QString &id, Lang lang)
{
    for (const auto &v : raw())
        if (v.toObject().value(u"id"_s).toString() == id)
            return localized(v.toObject(), lang);
    return {};
}

QString SchematicLibrary::digest(const QJsonObject &example)
{
    QHash<QString, QJsonObject> parts;
    QString out = u"Parts (reference, value, symbol, footprint):\n"_s;
    for (const auto &v : example.value(u"parts"_s).toArray()) {
        const QJsonObject p = v.toObject();
        parts.insert(p.value(u"ref"_s).toString(), p);
        out += u"- %1 %2 (%3), footprint %4\n"_s.arg(p.value(u"ref"_s).toString(), p.value(u"value"_s).toString(),
                                                       p.value(u"symbol"_s).toString(), p.value(u"footprint"_s).toString());
    }
    out += u"\nNets (net: reference.pin(pin name) …):\n"_s;
    for (const auto &v : example.value(u"nets"_s).toArray()) {
        const QJsonObject n = v.toObject();
        QStringList nodes;
        for (const auto &node : n.value(u"nodes"_s).toArray()) {
            const QString ref = node.toString().section(u'.', 0, 0), pin = node.toString().section(u'.', 1);
            const QString function = parts.value(ref).value(u"pins"_s).toObject().value(pin).toString();
            nodes.append(node.toString() + (function.isEmpty() || function == pin ? QString() : u'(' + function + u')'));
        }
        out += u"- "_s + n.value(u"name"_s).toString() + u": "_s + nodes.join(u", "_s) + u'\n';
    }
    return out;
}

QString SchematicLibrary::kicadNetlist(const QJsonObject &example)
{
    const QString id = example.value(u"id"_s).toString();
    // Stable ids: the same example always gives the same netlist.
    const QUuid ns = QUuid::fromString(u"{6f1c2a52-7d0e-4a36-9c1b-8e0b5a2f4d11}"_s);
    QHash<QString, QJsonObject> parts;
    QString out = u"(export (version \"E\")\n  (design\n    (source "_s + quoted(id + u".kicad_sch"_s)
                  + u")\n    (tool \"Jarvis\"))\n  (components"_s;
    for (const auto &v : example.value(u"parts"_s).toArray()) {
        const QJsonObject p = v.toObject();
        const QString ref = p.value(u"ref"_s).toString();
        parts.insert(ref, p);
        const auto [lib, name] = libPart(p.value(u"symbol"_s).toString());
        const QString uuid = QUuid::createUuidV5(ns, id + u'/' + ref).toString(QUuid::WithoutBraces);
        out += u"\n    (comp (ref %1)\n      (value %2)\n      (footprint %3)\n      (libsource (lib %4) (part %5) (description \"\"))\n"
               u"      (sheetpath (names \"/\") (tstamps \"/\"))\n      (tstamps %6))"_s
                   .arg(quoted(ref), quoted(p.value(u"value"_s).toString()), quoted(p.value(u"footprint"_s).toString()),
                        quoted(lib), quoted(name), quoted(uuid));
    }
    out += u")\n  (nets"_s;
    int code = 0;
    for (const auto &v : example.value(u"nets"_s).toArray()) {
        const QJsonObject n = v.toObject();
        out += u"\n    (net (code \"%1\") (name %2)"_s.arg(++code).arg(quoted(n.value(u"name"_s).toString()));
        for (const auto &node : n.value(u"nodes"_s).toArray()) {
            const QString ref = node.toString().section(u'.', 0, 0), pin = node.toString().section(u'.', 1);
            const QString function = parts.value(ref).value(u"pins"_s).toObject().value(pin).toString();
            out += u"\n      (node (ref %1) (pin %2)%3 (pintype \"passive\"))"_s
                       .arg(quoted(ref), quoted(pin), function.isEmpty() ? QString() : u" (pinfunction "_s + quoted(function) + u')');
        }
        out += u')';
    }
    return out + u"))\n"_s;
}

QString SchematicLibrary::bomCsv(const QJsonArray &parts)
{
    struct Row
    {
        QStringList refs;
        QString value, footprint, symbol;
    };
    QMap<QString, Row> rows;
    for (const auto &v : parts) {
        const QJsonObject p = v.toObject();
        const QString key = p.value(u"value"_s).toString() + u'\x1f' + p.value(u"footprint"_s).toString();
        Row &row = rows[key];
        row.refs.append(p.value(u"ref"_s).toString());
        row.value = p.value(u"value"_s).toString();
        row.footprint = p.value(u"footprint"_s).toString();
        row.symbol = p.value(u"symbol"_s).toString();
    }
    // R2 before R10: letters, then the number.
    static const QRegularExpression split(u"^([A-Za-z#_]*)(\\d*)"_s);
    auto natural = [](const QString &a, const QString &b) {
        const auto ma = split.match(a), mb = split.match(b);
        if (ma.captured(1) != mb.captured(1))
            return ma.captured(1) < mb.captured(1);
        return ma.captured(2).toInt() < mb.captured(2).toInt();
    };
    QList<Row> ordered;
    for (Row row : std::as_const(rows)) {
        std::sort(row.refs.begin(), row.refs.end(), natural);
        ordered.append(row);
    }
    std::sort(ordered.begin(), ordered.end(), [&](const Row &a, const Row &b) { return natural(a.refs.first(), b.refs.first()); });
    auto cell = [](QString s) {
        // A spreadsheet must not run a cell as a formula.
        if (!s.isEmpty() && QStringView(u"=+-@").contains(s.at(0)))
            s.prepend(u'\'');
        if (s.contains(u';') || s.contains(u'"') || s.contains(u'\n'))
            s = u'"' + s.replace(u'"', u"\"\""_s) + u'"';
        return s;
    };
    QString csv = u"Qty;References;Value;Footprint;Symbol\n"_s;
    for (const Row &row : std::as_const(ordered))
        csv += QString::number(row.refs.size()) + u';' + cell(row.refs.join(u", "_s)) + u';' + cell(row.value) + u';'
               + cell(row.footprint) + u';' + cell(row.symbol) + u'\n';
    return csv;
}

bool SchematicLibrary::answer(const QString &input, Lang lang, QString *reply)
{
    // Only an explicit request for an example: "покажи схему подключения DHT22" is Claude's to draw.
    static const QRegularExpression ask(
        u"(?:пример\\w*\\s+(?:\\S+\\s+)?схем|схем\\w*[\\s-]+пример|"
        u"examples?\\s+(?:of\\s+(?:a|an|the)\\s+)?(?:circuit|schematic)|(?:circuit|schematic)s?\\s+examples?)"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const QString text = input.trimmed();
    if (text.size() > 200 || !ask.match(text).hasMatch())
        return false;
    const bool ru = lang == Lang::Ru;
    const QSet<QString> wanted = text::stems(text);
    QJsonObject best;
    int bestScore = 0;
    for (const auto &v : raw()) {
        const QJsonObject e = v.toObject();
        QString words = e.value(u"title"_s).toString() + u' ' + e.value(u"title_en"_s).toString();
        for (const auto &t : e.value(u"tags"_s).toArray())
            words += u' ' + t.toString();
        const int score = int((wanted & text::stems(words)).size());
        if (score > bestScore) {
            bestScore = score;
            best = e;
        }
    }
    const QString where = ru ? u"Рисунок, SPICE-модель и нетлист для KiCad — «Центр управления → Навыки → Электроника → Схемы»."_s
                             : u"The drawing, a SPICE deck and a KiCad netlist: Control center → Skills → Electronics → Schematics."_s;
    if (bestScore == 0) {
        // "пример схемы подключения DHT22": nothing like it here — Claude answers that.
        if (text.split(u' ', Qt::SkipEmptyParts).size() > 5)
            return false;
        QStringList titles;
        for (const auto &v : list(lang))
            titles.append(u"- "_s + v.toObject().value(u"title"_s).toString());
        *reply = (ru ? u"Есть такие примеры схем:\n"_s : u"These example circuits are available:\n"_s) + titles.join(u'\n')
                 + u"\n\n"_s + where;
        return true;
    }
    const QJsonObject e = localized(best, lang);
    *reply = u"**"_s + e.value(u"title"_s).toString() + u"**\n\n"_s + e.value(u"description"_s).toString() + u"\n\n```text\n"_s
             + digest(e) + u"```\n\n"_s + where;
    return true;
}

} // namespace jarvis
