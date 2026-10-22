#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

namespace jarvis {

// A file the user attached to a question, prepared for Claude:
//  * images (PNG, JPEG, GIF, WebP) — scaled down when large;
//  * PDF (datasheets, articles) — sent as a document Claude reads with its pages;
//  * source code and text — as text with the file name;
//  * schematics: KiCad .kicad_sch (netlist and a PDF of the drawing through
//    kicad-cli when it is installed), .kicad_pcb, KiCad netlists (.net) and
//    SimulIDE circuits (.sim1/.simu) — as a list of parts and of what each
//    net connects, which Claude reads far better than the raw file.
struct Attachment
{
    QString name;
    QString kind;     // image, pdf, text, schematic
    QString summary;  // "schematic: 23 parts, 28 nets" — for the chat and history
    QString digest;   // schematics: the parts and nets as text (also shown offline)
    QJsonArray blocks; // Claude content blocks
    QString error;    // set when the file cannot be used
};

class Attachments
{
public:
    static constexpr qint64 kMaxTextBytes = 400 * 1024;
    static constexpr qint64 kMaxImageBytes = 20 * 1024 * 1024;
    static constexpr qint64 kMaxPdfBytes = 25 * 1024 * 1024;
    static constexpr int kMaxFiles = 8;

    static Attachment load(const QString &path);
    // Name filters for a file dialog.
    static QStringList dialogFilters();
    static bool isSchematic(const QString &path);

    // Parsers, public for tests: the text Claude gets for a schematic.
    static QString kicadNetlist(const QByteArray &netFile, QString *summary);
    static QString kicadSchematic(const QByteArray &schFile, QString *summary);
    static QString kicadPcb(const QByteArray &pcbFile, QString *summary);
    static QString simulide(const QByteArray &circuit, QString *summary);
    // Parts of a KiCad netlist (.net) or of one schematic sheet (.kicad_sch):
    // [{ref, value, footprint, symbol}], power symbols left out; [] when unreadable.
    static QJsonArray kicadParts(const QByteArray &file);
};

} // namespace jarvis
