#include "attachments.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

constexpr int kMaxDigestChars = 60000;

// ---- KiCad S-expressions ---------------------------------------------------------------

struct Sx
{
    QString atom;
    QList<Sx> items;
    bool list = false;

    const Sx *child(QStringView head) const
    {
        for (const Sx &i : items)
            if (i.list && !i.items.isEmpty() && i.items.first().atom == head)
                return &i;
        return nullptr;
    }
    QList<const Sx *> children(QStringView head) const
    {
        QList<const Sx *> out;
        for (const Sx &i : items)
            if (i.list && !i.items.isEmpty() && i.items.first().atom == head)
                out.append(&i);
        return out;
    }
    QString at(int index) const { return index < items.size() ? items.at(index).atom : QString(); }
    // (head "value") -> value
    QString field(QStringView head) const
    {
        const Sx *c = child(head);
        return c ? c->at(1) : QString();
    }
    // (property "Reference" "R1") -> R1
    QString property(QStringView name) const
    {
        for (const Sx *p : children(u"property"))
            if (p->at(1) == name)
                return p->at(2);
        return {};
    }
};

class SxParser
{
public:
    explicit SxParser(const QString &text)
        : m_s(text)
    {
    }
    bool parse(Sx *root)
    {
        skip();
        if (m_pos >= m_s.size() || m_s.at(m_pos) != u'(')
            return false;
        return node(root, 0);
    }

private:
    void skip()
    {
        while (m_pos < m_s.size() && m_s.at(m_pos).isSpace())
            ++m_pos;
    }
    bool node(Sx *out, int depth)
    {
        if (depth > 200)
            return false;
        out->list = true;
        ++m_pos; // (
        for (;;) {
            skip();
            if (m_pos >= m_s.size())
                return false;
            const QChar c = m_s.at(m_pos);
            if (c == u')') {
                ++m_pos;
                return true;
            }
            Sx item;
            if (c == u'(') {
                if (!node(&item, depth + 1))
                    return false;
            } else if (c == u'"') {
                ++m_pos;
                QString value;
                while (m_pos < m_s.size() && m_s.at(m_pos) != u'"') {
                    if (m_s.at(m_pos) == u'\\' && m_pos + 1 < m_s.size()) {
                        ++m_pos;
                        value += m_s.at(m_pos) == u'n' ? QChar(u'\n') : m_s.at(m_pos);
                    } else {
                        value += m_s.at(m_pos);
                    }
                    ++m_pos;
                }
                ++m_pos;
                item.atom = value;
            } else {
                const int start = m_pos;
                while (m_pos < m_s.size() && !m_s.at(m_pos).isSpace() && m_s.at(m_pos) != u'(' && m_s.at(m_pos) != u')')
                    ++m_pos;
                item.atom = m_s.mid(start, m_pos - start);
            }
            out->items.append(std::move(item));
        }
    }

    QString m_s;
    int m_pos = 0;
};

bool parseSx(const QByteArray &bytes, Sx *root)
{
    return SxParser(QString::fromUtf8(bytes)).parse(root);
}

QString clip(QString text, bool *clipped = nullptr)
{
    if (text.size() <= kMaxDigestChars)
        return text;
    if (clipped)
        *clipped = true;
    text.truncate(kMaxDigestChars);
    return text + u"\n… (the list is longer; the rest was left out)\n"_s;
}

// ---- Helpers --------------------------------------------------------------------------

QString languageFor(const QString &suffix)
{
    static const QHash<QString, QString> table = {
        {u"c"_s, u"c"_s}, {u"h"_s, u"c"_s}, {u"ino"_s, u"cpp"_s}, {u"cpp"_s, u"cpp"_s}, {u"cc"_s, u"cpp"_s},
        {u"cxx"_s, u"cpp"_s}, {u"hpp"_s, u"cpp"_s}, {u"hh"_s, u"cpp"_s}, {u"py"_s, u"python"_s},
        {u"js"_s, u"javascript"_s}, {u"ts"_s, u"typescript"_s}, {u"qml"_s, u"qml"_s}, {u"json"_s, u"json"_s},
        {u"xml"_s, u"xml"_s}, {u"yaml"_s, u"yaml"_s}, {u"yml"_s, u"yaml"_s}, {u"toml"_s, u"toml"_s},
        {u"ini"_s, u"ini"_s}, {u"md"_s, u"markdown"_s}, {u"sh"_s, u"bash"_s}, {u"bash"_s, u"bash"_s},
        {u"cmake"_s, u"cmake"_s}, {u"v"_s, u"verilog"_s}, {u"sv"_s, u"systemverilog"_s}, {u"vhd"_s, u"vhdl"_s},
        {u"vhdl"_s, u"vhdl"_s}, {u"s"_s, u"asm"_s}, {u"asm"_s, u"asm"_s}, {u"ld"_s, u"ld"_s}, {u"rs"_s, u"rust"_s},
        {u"go"_s, u"go"_s}, {u"java"_s, u"java"_s}, {u"kt"_s, u"kotlin"_s}, {u"cs"_s, u"csharp"_s},
        {u"lua"_s, u"lua"_s}, {u"html"_s, u"html"_s}, {u"css"_s, u"css"_s}, {u"sql"_s, u"sql"_s},
        {u"cir"_s, u"spice"_s}, {u"sp"_s, u"spice"_s}, {u"spice"_s, u"spice"_s}, {u"csv"_s, u"csv"_s},
        {u"dts"_s, u"dts"_s}, {u"ioc"_s, u"ini"_s}};
    return table.value(suffix.toLower());
}

QJsonObject textBlock(const QString &text) { return QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}; }

QJsonObject base64Block(const QString &type, const QString &media, const QByteArray &data, const QString &title = {})
{
    QJsonObject block{{u"type"_s, type},
                      {u"source"_s, QJsonObject{{u"type"_s, u"base64"_s}, {u"media_type"_s, media},
                                                {u"data"_s, QString::fromLatin1(data.toBase64())}}}};
    if (!title.isEmpty())
        block[u"title"_s] = title.left(200);
    return block;
}

QString kicadCli()
{
    return QStandardPaths::findExecutable(u"kicad-cli"_s);
}

bool runKicadCli(const QStringList &args, int timeoutMs = 30000)
{
    const QString cli = kicadCli();
    if (cli.isEmpty())
        return false;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(cli, args);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}


} // namespace

// KiCad escapes characters in names: "SDA{slash}A4" is "SDA/A4".
QString unescape(QString name)
{
    static const QList<std::pair<QString, QString>> table = {
        {u"{slash}"_s, u"/"_s}, {u"{backslash}"_s, u"\\"_s}, {u"{lt}"_s, u"<"_s}, {u"{gt}"_s, u">"_s},
        {u"{dblquote}"_s, u"\""_s}, {u"{colon}"_s, u":"_s}, {u"{space}"_s, u" "_s}, {u"{tab}"_s, u" "_s},
        {u"{return}"_s, u" "_s}, {u"{brace}"_s, u"{"_s}};
    for (const auto &[from, to] : table)
        name.replace(from, to);
    return name;
}

// ---- KiCad netlist (.net, kicadsexpr) ----------------------------------------------------

QString Attachments::kicadNetlist(const QByteArray &netFile, QString *summary)
{
    Sx root;
    if (!parseSx(netFile, &root) || root.at(0) != u"export")
        return {};
    QString out = u"Parts (reference, value, symbol, footprint):\n"_s;
    int parts = 0;
    if (const Sx *components = root.child(u"components")) {
        for (const Sx *c : components->children(u"comp")) {
            ++parts;
            const Sx *lib = c->child(u"libsource");
            QString symbol = lib ? lib->field(u"lib") + u':' + lib->field(u"part") : QString();
            out += u"- "_s + c->field(u"ref") + u" "_s + c->field(u"value");
            if (!symbol.isEmpty() && symbol != u":")
                out += u" ("_s + symbol + u')';
            const QString fp = c->field(u"footprint");
            if (!fp.isEmpty())
                out += u", footprint "_s + fp;
            out += u'\n';
        }
    }
    out += u"\nNets (net: reference.pin(pin name) …):\n"_s;
    int nets = 0;
    if (const Sx *list = root.child(u"nets")) {
        for (const Sx *n : list->children(u"net")) {
            QStringList nodes;
            for (const Sx *node : n->children(u"node")) {
                QString pin = node->field(u"ref") + u'.' + node->field(u"pin");
                const QString function = node->field(u"pinfunction");
                if (!function.isEmpty() && function != u"~")
                    pin += u'(' + function + u')';
                nodes.append(pin);
            }
            QString name = unescape(n->field(u"name"));
            // One-pin nets are unconnected pins: list them separately.
            if (nodes.size() < 2) {
                if (!name.startsWith(u"unconnected-"))
                    out += u"- "_s + name + u": "_s + nodes.join(u", "_s) + u"  ← only one pin\n"_s;
                continue;
            }
            ++nets;
            out += u"- "_s + name + u": "_s + nodes.join(u", "_s) + u'\n';
        }
    }
    if (summary)
        *summary = u"%1 parts, %2 nets"_s.arg(parts).arg(nets);
    return clip(out);
}

// ---- KiCad schematic without kicad-cli ---------------------------------------------------

QString Attachments::kicadSchematic(const QByteArray &schFile, QString *summary)
{
    Sx root;
    if (!parseSx(schFile, &root) || root.at(0) != u"kicad_sch")
        return {};
    QString out = u"Parts (reference, value, symbol, footprint):\n"_s;
    QSet<QString> power;
    int parts = 0;
    for (const Sx *s : root.children(u"symbol")) {
        const QString ref = s->property(u"Reference");
        const QString value = s->property(u"Value");
        if (ref.startsWith(u'#')) { // power symbols and flags
            if (s->field(u"lib_id").startsWith(u"power:"))
                power.insert(value);
            continue;
        }
        ++parts;
        out += u"- "_s + ref + u" "_s + value + u" ("_s + s->field(u"lib_id") + u')';
        const QString fp = s->property(u"Footprint");
        if (!fp.isEmpty())
            out += u", footprint "_s + fp;
        out += u'\n';
    }
    QStringList labels;
    for (const char16_t *kind : {u"label", u"global_label", u"hierarchical_label"})
        for (const Sx *l : root.children(kind))
            if (!labels.contains(unescape(l->at(1))))
                labels.append(unescape(l->at(1)));
    if (!power.isEmpty())
        out += u"\nPower nets: "_s + QStringList(power.begin(), power.end()).join(u", "_s) + u'\n';
    if (!labels.isEmpty())
        out += u"Net labels: "_s + labels.join(u", "_s) + u'\n';
    QStringList sheets;
    for (const Sx *sheet : root.children(u"sheet"))
        sheets.append(sheet->property(u"Sheetname") + u" ("_s + sheet->property(u"Sheetfile") + u')');
    if (!sheets.isEmpty())
        out += u"Sub-sheets (not included): "_s + sheets.join(u", "_s) + u'\n';
    QStringList notes;
    for (const Sx *t : root.children(u"text"))
        if (notes.size() < 20 && !t->at(1).trimmed().isEmpty())
            notes.append(t->at(1).simplified().left(200));
    if (!notes.isEmpty())
        out += u"Text notes: "_s + notes.join(u" | "_s) + u'\n';
    out += u"Drawing: %1 wires, %2 junctions, %3 no-connect flags.\n"_s.arg(root.children(u"wire").size())
               .arg(root.children(u"junction").size()).arg(root.children(u"no_connect").size());
    out += u"Pin-to-pin connections were not extracted (kicad-cli is not available); judge them from labels.\n"_s;
    if (summary)
        *summary = u"%1 parts"_s.arg(parts);
    return clip(out);
}

// ---- KiCad PCB ------------------------------------------------------------------------------

QString Attachments::kicadPcb(const QByteArray &pcbFile, QString *summary)
{
    Sx root;
    if (!parseSx(pcbFile, &root) || root.at(0) != u"kicad_pcb")
        return {};
    QMap<QString, QStringList> nets;
    QString parts = u"Footprints (reference, value, footprint, side):\n"_s;
    int count = 0;
    const auto footprints = root.children(u"footprint") + root.children(u"module");
    for (const Sx *f : footprints) {
        QString ref = f->property(u"Reference"), value = f->property(u"Value");
        for (const Sx *t : f->children(u"fp_text")) {
            if (ref.isEmpty() && t->at(1) == u"reference")
                ref = t->at(2);
            if (value.isEmpty() && t->at(1) == u"value")
                value = t->at(2);
        }
        ++count;
        parts += u"- "_s + ref + u" "_s + value + u" ("_s + f->at(1) + u", "_s
                 + (f->field(u"layer").startsWith(u"B.") ? u"bottom"_s : u"top"_s) + u")\n"_s;
        for (const Sx *pad : f->children(u"pad")) {
            const Sx *net = pad->child(u"net");
            if (!net)
                continue;
            const QString name = unescape(net->items.size() > 2 ? net->at(2) : net->at(1));
            if (!name.isEmpty())
                nets[name].append(ref + u'.' + pad->at(1));
        }
    }
    int copper = 0;
    if (const Sx *layers = root.child(u"layers"))
        for (const Sx &l : layers->items)
            copper += l.list && l.at(1).endsWith(u".Cu");
    QString out = parts + u"\nNets (from pads):\n"_s;
    int netCount = 0;
    for (auto it = nets.cbegin(); it != nets.cend(); ++it) {
        if (it.value().size() < 2 && it.key().startsWith(u"unconnected-"))
            continue;
        ++netCount;
        out += u"- "_s + it.key() + u": "_s + it.value().join(u", "_s) + u'\n';
    }
    out += u"\nBoard: %1 copper layers, %2 track segments, %3 vias, %4 zones.\n"_s.arg(copper)
               .arg(root.children(u"segment").size()).arg(root.children(u"via").size()).arg(root.children(u"zone").size());
    if (summary)
        *summary = u"%1 footprints, %2 nets, %3 layers"_s.arg(count).arg(netCount).arg(copper);
    return clip(out);
}

// ---- SimulIDE -------------------------------------------------------------------------------

QString Attachments::simulide(const QByteArray &circuit, QString *summary)
{
    QXmlStreamReader xml(circuit);
    struct Part
    {
        QString id, type;
        QStringList properties;
    };
    QList<Part> parts;
    QSet<QString> nodes;
    QList<std::pair<QString, QString>> wires;
    bool circuitRoot = false;
    static const QSet<QString> skip = {
        u"itemtype"_s, u"CircId"_s, u"id"_s, u"objectName"_s, u"uid"_s, u"Pos"_s, u"x"_s, u"y"_s, u"rotation"_s,
        u"hflip"_s, u"vflip"_s, u"Label"_s, u"label"_s, u"idLabPos"_s, u"labelrot"_s, u"labelx"_s, u"labely"_s,
        u"valLabPos"_s, u"valLabRot"_s, u"valLabelx"_s, u"valLabely"_s, u"valLabRot"_s, u"Show_id"_s, u"Show_Val"_s,
        u"mainCompProps"_s, u"pointList"_s, u"startpinid"_s, u"endpinid"_s, u"enodeid"_s, u"Show_Res"_s,
        u"Show_Volt"_s, u"Show_Ind"_s, u"Show_Cap"_s, u"Mirror"_s, u"boardPos"_s, u"circPos"_s, u"boardRot"_s,
        u"circRot"_s, u"boardHflip"_s, u"boardVflip"_s};
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == u"circuit") {
            circuitRoot = true;
            continue;
        }
        if (xml.name() != u"item")
            continue;
        const auto a = xml.attributes();
        const QString type = a.value(u"itemtype").toString();
        QString id = a.value(u"CircId").toString();
        for (const char16_t *key : {u"id", u"objectName", u"uid"})
            if (id.isEmpty())
                id = a.value(key).toString();
        if (type == u"Connector") {
            wires.append({a.value(u"startpinid").toString(), a.value(u"endpinid").toString()});
        } else if (type == u"Node") {
            nodes.insert(id);
        } else if (!type.isEmpty() && !id.isEmpty()) {
            Part p{id, type, {}};
            for (const auto &attr : a)
                if (!skip.contains(attr.name().toString()) && !attr.value().isEmpty() && attr.value().size() < 120)
                    p.properties.append(attr.name().toString() + u'=' + attr.value().toString());
            parts.append(p);
        }
    }
    if (!circuitRoot || xml.hasError())
        return {};
    // Pin ids are "<component id>-<pin>"; a node's pins all belong to one net.
    QStringList owners;
    for (const Part &p : parts)
        owners.append(p.id);
    owners += QStringList(nodes.begin(), nodes.end());
    std::sort(owners.begin(), owners.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    auto owner = [&](const QString &pin) {
        for (const QString &o : std::as_const(owners))
            if (pin.startsWith(o + u'-'))
                return o;
        return QString();
    };
    QHash<QString, QString> parent;
    std::function<QString(const QString &)> find = [&](const QString &x) {
        QString root = x;
        while (parent.contains(root) && parent.value(root) != root)
            root = parent.value(root);
        return root;
    };
    auto unite = [&](const QString &a, const QString &b) {
        const QString ra = find(a), rb = find(b);
        if (ra != rb)
            parent[ra] = rb;
    };
    auto key = [&](const QString &pin) {
        const QString o = owner(pin);
        return nodes.contains(o) ? o : pin; // every pin of a node is the node
    };
    for (const auto &[a, b] : std::as_const(wires))
        if (!a.isEmpty() && !b.isEmpty())
            unite(key(a), key(b));
    QMap<QString, QStringList> groups;
    for (const auto &[a, b] : std::as_const(wires))
        for (const QString &pin : {a, b}) {
            if (pin.isEmpty() || nodes.contains(owner(pin)))
                continue;
            QString label = pin;
            const QString o = owner(pin);
            if (!o.isEmpty())
                label = o + u'.' + pin.mid(o.size() + 1);
            QStringList &g = groups[find(key(pin))];
            if (!g.contains(label))
                g.append(label);
        }
    QString out = u"Parts (id, type, properties):\n"_s;
    for (const Part &p : std::as_const(parts))
        out += u"- "_s + p.id + u" ("_s + p.type + u")"_s + (p.properties.isEmpty() ? QString() : u": "_s + p.properties.join(u", "_s)) + u'\n';
    out += u"\nNets (what each wire group connects):\n"_s;
    int n = 0;
    for (const QStringList &g : std::as_const(groups)) {
        QStringList sorted = g;
        sorted.sort();
        out += u"- net %1: "_s.arg(++n) + sorted.join(u", "_s) + u'\n';
    }
    if (summary)
        *summary = u"%1 parts, %2 nets"_s.arg(parts.size()).arg(n);
    return clip(out);
}

// ---- Loading ----------------------------------------------------------------------------------

bool Attachments::isSchematic(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == u"kicad_sch" || s == u"kicad_pcb" || s == u"kicad_pro" || s == u"net" || s == u"sim1" || s == u"simu";
}

QStringList Attachments::dialogFilters()
{
    return {u"Files Jarvis can read (*.png *.jpg *.jpeg *.gif *.webp *.pdf *.kicad_sch *.kicad_pcb *.kicad_pro *.net *.sim1 *.simu "
            u"*.c *.h *.cpp *.hpp *.ino *.py *.js *.ts *.qml *.json *.xml *.yaml *.yml *.txt *.md *.csv *.log *.sh *.cmake "
            u"*.ini *.toml *.v *.sv *.vhd *.s *.asm *.ld *.rs *.go *.java *.cir *.sp *.ioc CMakeLists.txt Makefile)"_s,
            u"Images (*.png *.jpg *.jpeg *.gif *.webp)"_s, u"PDF (*.pdf)"_s,
            u"Schematics (*.kicad_sch *.kicad_pcb *.kicad_pro *.net *.sim1 *.simu)"_s, u"All files (*)"_s};
}

Attachment Attachments::load(const QString &path)
{
    Attachment a;
    QFileInfo info(path);
    a.name = info.fileName();
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !info.isFile() || !info.isReadable()) {
        a.error = QCoreApplication::translate("jarvis", "Cannot read %1").arg(a.name);
        return a;
    }
    // Device and process pseudo-files are never attachments.
    for (const char16_t *system : {u"/proc/", u"/sys/", u"/dev/"})
        if (canonical.startsWith(QStringView(system))) {
            a.error = QCoreApplication::translate("jarvis", "%1 is not a regular file").arg(a.name);
            return a;
        }
    QString suffix = info.suffix().toLower();
    const qint64 size = info.size();

    // A KiCad project stands for its schematic.
    QString file = canonical;
    if (suffix == u"kicad_pro") {
        file = info.absolutePath() + u'/' + info.completeBaseName() + u".kicad_sch"_s;
        if (!QFileInfo::exists(file)) {
            a.error = QCoreApplication::translate("jarvis", "%1: the schematic next to the project was not found").arg(a.name);
            return a;
        }
        suffix = u"kicad_sch"_s;
    }

    static const QHash<QString, QString> images = {{u"png"_s, u"image/png"_s}, {u"jpg"_s, u"image/jpeg"_s},
                                                   {u"jpeg"_s, u"image/jpeg"_s}, {u"gif"_s, u"image/gif"_s},
                                                   {u"webp"_s, u"image/webp"_s}};
    if (images.contains(suffix)) {
        if (size > kMaxImageBytes) {
            a.error = QCoreApplication::translate("jarvis", "%1 is larger than 20 MB").arg(a.name);
            return a;
        }
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            a.error = f.errorString();
            return a;
        }
        QByteArray data = f.readAll();
        QString media = images.value(suffix);
        // Claude reads up to ~1568 px on the long side; bigger pictures only cost more.
        QImage image;
        if (image.loadFromData(data) && (std::max(image.width(), image.height()) > 1568 || data.size() > 3'500'000)) {
            image = image.scaled(1568, 1568, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QBuffer buffer(&data);
            data.clear();
            buffer.open(QIODevice::WriteOnly);
            const bool alpha = image.hasAlphaChannel();
            image.save(&buffer, alpha ? "PNG" : "JPEG", alpha ? -1 : 85);
            media = alpha ? u"image/png"_s : u"image/jpeg"_s;
        }
        a.kind = u"image"_s;
        a.summary = u"image %1"_s.arg(a.name);
        a.blocks.append(textBlock(u"Attached image: "_s + a.name));
        a.blocks.append(base64Block(u"image"_s, media, data));
        return a;
    }
    if (suffix == u"pdf") {
        if (size > kMaxPdfBytes) {
            a.error = QCoreApplication::translate("jarvis", "%1 is larger than 25 MB").arg(a.name);
            return a;
        }
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            a.error = f.errorString();
            return a;
        }
        a.kind = u"pdf"_s;
        a.summary = u"PDF %1"_s.arg(a.name);
        a.blocks.append(base64Block(u"document"_s, u"application/pdf"_s, f.readAll(), a.name));
        return a;
    }

    // Everything else is text: code, schematics, logs.
    if (size > 8 * 1024 * 1024) {
        a.error = QCoreApplication::translate("jarvis", "%1 is too large").arg(a.name);
        return a;
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        a.error = f.errorString();
        return a;
    }
    const QByteArray bytes = f.readAll();
    if (bytes.left(8192).contains('\0')) {
        a.error = QCoreApplication::translate("jarvis", "%1 is a binary file: attach an image, a PDF, a schematic or text").arg(a.name);
        return a;
    }

    QString digest, summary, source;
    if (suffix == u"kicad_sch") {
        QTemporaryDir dir;
        const QString net = dir.path() + u"/schematic.net"_s, pdf = dir.path() + u"/schematic.pdf"_s;
        if (dir.isValid() && runKicadCli({u"sch"_s, u"export"_s, u"netlist"_s, u"--format"_s, u"kicadsexpr"_s, u"-o"_s, net, file})) {
            QFile n(net);
            if (n.open(QIODevice::ReadOnly))
                digest = kicadNetlist(n.readAll(), &summary);
            source = u"KiCad schematic, netlist exported with kicad-cli (all sheets)"_s;
        }
        if (digest.isEmpty()) {
            digest = kicadSchematic(bytes, &summary);
            source = u"KiCad schematic"_s;
        }
        // The drawing itself: Claude sees the layout, notes and values.
        if (dir.isValid() && runKicadCli({u"sch"_s, u"export"_s, u"pdf"_s, u"-o"_s, pdf, file})) {
            QFile p(pdf);
            if (p.open(QIODevice::ReadOnly) && p.size() < kMaxPdfBytes)
                a.blocks.append(base64Block(u"document"_s, u"application/pdf"_s, p.readAll(), a.name + u" (drawing)"_s));
        }
    } else if (suffix == u"kicad_pcb") {
        digest = kicadPcb(bytes, &summary);
        source = u"KiCad PCB"_s;
    } else if (suffix == u"net" && bytes.trimmed().startsWith("(export")) {
        digest = kicadNetlist(bytes, &summary);
        source = u"KiCad netlist"_s;
    } else if (suffix == u"sim1" || suffix == u"simu") {
        digest = simulide(bytes, &summary);
        source = u"SimulIDE circuit"_s;
    }
    if (!digest.isEmpty()) {
        a.kind = u"schematic"_s;
        a.digest = digest;
        a.summary = u"%1 — %2"_s.arg(a.name, summary);
        a.blocks.prepend(textBlock(u"Attached "_s + source + u": "_s + a.name + u" ("_s + summary + u")\n"_s + digest));
        return a;
    }
    if (Attachments::isSchematic(file)) {
        a.error = QCoreApplication::translate("jarvis", "%1: could not read the schematic").arg(a.name);
        return a;
    }

    auto decoder = QStringDecoder(QStringDecoder::Utf8);
    QString text = decoder(bytes.left(kMaxTextBytes));
    if (decoder.hasError()) {
        a.error = QCoreApplication::translate("jarvis", "%1 is not UTF-8 text").arg(a.name);
        return a;
    }
    const bool cut = bytes.size() > kMaxTextBytes;
    QString language = languageFor(suffix);
    if (a.name == u"CMakeLists.txt")
        language = u"cmake"_s;
    else if (a.name == u"Makefile")
        language = u"makefile"_s;
    a.kind = u"text"_s;
    a.summary = u"%1 (%2 lines)"_s.arg(a.name).arg(text.count(u'\n') + 1);
    a.blocks.append(textBlock(u"Attached file "_s + a.name + (cut ? u" (first 400 KB of %1 KB)"_s.arg(bytes.size() / 1024) : QString())
                              + u":\n```"_s + language + u'\n' + text + u"\n```"_s));
    return a;
}

} // namespace jarvis
