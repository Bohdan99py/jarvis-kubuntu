#include "markdown_safe.h"

#include <QRegularExpression>
#include <QStringList>

namespace jarvis::text {
namespace {

// Outside code: images turn into their alt text in brackets, "![" of a
// reference image ("![a][1]", "![a]") into a plain "[", "<" into an entity.
QString prose(QString part)
{
    // One level of parentheses inside the URL, as in ".../File_(1).png".
    static const QRegularExpression inlineImage(QStringLiteral("!\\[([^\\]]*)\\]\\((?:[^()]|\\([^()]*\\))*\\)"));
    part.replace(inlineImage, QStringLiteral("[\\1]"));
    part.replace(QLatin1String("!["), QLatin1String("["));
    part.replace(u'<', QLatin1String("&lt;"));
    return part;
}

// A code span opens and closes with backtick runs of the same length.
QString line(const QString &text)
{
    QString out;
    qsizetype pos = 0;
    while (pos < text.size()) {
        const qsizetype open = text.indexOf(u'`', pos);
        if (open < 0)
            break;
        qsizetype run = open;
        while (run < text.size() && text.at(run) == u'`')
            ++run;
        const QString fence(run - open, u'`');
        qsizetype close = text.indexOf(fence, run);
        // The closing run must be exactly as long ("``" does not close "`").
        while (close >= 0 && close + fence.size() < text.size() && text.at(close + fence.size()) == u'`') {
            qsizetype skip = close;
            while (skip < text.size() && text.at(skip) == u'`')
                ++skip;
            close = text.indexOf(fence, skip);
        }
        if (close < 0) {
            // Unbalanced: everything from here on is prose, backticks included.
            out += prose(text.mid(pos));
            return out;
        }
        out += prose(text.mid(pos, open - pos));
        out += text.mid(open, close + fence.size() - open);
        pos = close + fence.size();
    }
    out += prose(text.mid(pos));
    return out;
}

} // namespace

QString safeMarkdown(const QString &text)
{
    QStringList lines = text.split(u'\n');
    QString fence;
    for (QString &l : lines) {
        const QString trimmed = l.trimmed();
        if (fence.isEmpty() && (trimmed.startsWith(QLatin1String("```")) || trimmed.startsWith(QLatin1String("~~~")))) {
            qsizetype n = 0;
            while (n < trimmed.size() && trimmed.at(n) == trimmed.at(0))
                ++n;
            fence = trimmed.left(n);
            continue;
        }
        if (!fence.isEmpty()) {
            // Only the same marker, at least as long and alone on its line, closes the block.
            if (trimmed.startsWith(fence) && trimmed.count(fence.at(0)) == trimmed.size())
                fence.clear();
            continue;
        }
        l = line(l);
    }
    return lines.join(u'\n');
}

} // namespace jarvis::text
