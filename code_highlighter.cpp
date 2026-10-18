#include "code_highlighter.h"

#include <QColor>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

namespace {

QTextCharFormat style(const char *color, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(QColor(QLatin1String(color)));
    if (bold)
        f.setFontWeight(QFont::DemiBold);
    f.setFontItalic(italic);
    return f;
}

QString words(const char *list)
{
    return QStringLiteral("\\b(?:") + QString::fromLatin1(list).simplified().replace(u' ', u'|') + QStringLiteral(")\\b");
}

} // namespace

class Highlighter : public QSyntaxHighlighter
{
public:
    Highlighter(QTextDocument *document, const QString &language)
        : QSyntaxHighlighter(document)
    {
        const QString l = language.toLower();
        const auto keyword = style("#c58af9", true), type = style("#5aa2f0"), string = style("#9ad04f"),
                   number = style("#f0b45a"), comment = style("#6f7f92", false, true), preproc = style("#f07a8f"),
                   call = style("#7fd4e0");
        auto add = [this](const QString &pattern, const QTextCharFormat &f, bool caseless = false) {
            m_rules.append({QRegularExpression(pattern, caseless ? QRegularExpression::CaseInsensitiveOption
                                                                 : QRegularExpression::NoPatternOption), f});
        };
        const bool cfamily = QStringList{QStringLiteral("c"), QStringLiteral("cpp"), QStringLiteral("c++"), QStringLiteral("h"),
                                         QStringLiteral("hpp"), QStringLiteral("ino"), QStringLiteral("arduino"), QStringLiteral("cc"),
                                         QStringLiteral("java"), QStringLiteral("cs"), QStringLiteral("csharp"), QStringLiteral("kotlin"),
                                         QStringLiteral("glsl"), QStringLiteral("dts")}.contains(l);
        const bool script = QStringList{QStringLiteral("js"), QStringLiteral("javascript"), QStringLiteral("ts"),
                                        QStringLiteral("typescript"), QStringLiteral("qml"), QStringLiteral("json")}.contains(l);
        const bool hashComments = QStringList{QStringLiteral("python"), QStringLiteral("py"), QStringLiteral("bash"),
                                              QStringLiteral("sh"), QStringLiteral("shell"), QStringLiteral("console"),
                                              QStringLiteral("zsh"), QStringLiteral("cmake"), QStringLiteral("makefile"),
                                              QStringLiteral("make"), QStringLiteral("yaml"), QStringLiteral("yml"),
                                              QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("dockerfile")}.contains(l);
        m_blockComments = cfamily || script || l == u"rust" || l == u"go" || l == u"css" || l == u"verilog" || l == u"sql";

        add(QStringLiteral("\\b[A-Za-z_]\\w*(?=\\s*\\()"), call);
        if (cfamily) {
            add(words("auto break case char const continue default do double else enum extern float for goto if inline int "
                      "long register restrict return short signed sizeof static struct switch typedef union unsigned void "
                      "volatile while bool true false nullptr NULL class public private protected virtual override final "
                      "template typename namespace using new delete this operator friend constexpr consteval static_cast "
                      "reinterpret_cast const_cast dynamic_cast try catch throw noexcept explicit mutable import package "
                      "interface extends implements"), keyword);
            add(QStringLiteral("\\b(?:u?int(?:8|16|32|64)_t|size_t|uint|byte|word|String|std|Q[A-Z]\\w*|[A-Z][A-Z0-9_]{2,})\\b"), type);
            add(QStringLiteral("^\\s*#\\s*\\w+.*"), preproc);
        } else if (script) {
            add(words("var let const function return if else for while do switch case break continue new this class extends "
                      "import export from default try catch finally throw typeof instanceof async await yield null undefined "
                      "true false property signal readonly required alias id component pragma of in"), keyword);
            add(QStringLiteral("\\b[A-Z]\\w*\\b"), type);
        } else if (l == u"rust") {
            add(words("fn let mut pub struct enum impl trait use mod crate self Self match if else loop while for in return as "
                      "ref move where unsafe async await dyn true false const static type"), keyword);
            add(QStringLiteral("\\b(?:[iu](?:8|16|32|64|128|size)|f32|f64|bool|char|str|String|Vec|Option|Result|[A-Z]\\w*)\\b"), type);
        } else if (l == u"go") {
            add(words("func package import var const type struct interface map chan go defer select range return if else for "
                      "switch case default break continue fallthrough nil true false"), keyword);
        } else if (l == u"python" || l == u"py") {
            add(words("def class return if elif else for while in not and or is None True False import from as with try except "
                      "finally raise lambda yield pass break continue global nonlocal async await self"), keyword);
            add(QStringLiteral("^\\s*@\\w+"), preproc);
        } else if (l == u"bash" || l == u"sh" || l == u"shell" || l == u"console" || l == u"zsh") {
            add(words("if then else elif fi for while until do done case esac function in return local export readonly exit "
                      "set source cd sudo echo"), keyword);
            add(QStringLiteral("\\$\\{?\\w+\\}?"), type);
        } else if (l == u"cmake") {
            add(QStringLiteral("\\$\\{\\w+\\}"), type);
            add(words("PUBLIC PRIVATE INTERFACE REQUIRED COMPONENTS VERSION"), keyword);
        } else if (l == u"sql") {
            add(words("select from where insert into values update set delete create table drop alter join left right inner "
                      "on group by order having limit and or not null primary key as distinct"), keyword, true);
        } else if (l == u"asm" || l == u"s" || l == u"nasm") {
            add(QStringLiteral("^\\s*\\.\\w+"), preproc);
            add(QStringLiteral("\\b[rRxXwW]\\d+\\b|\\b(?:sp|lr|pc|r[0-9]{1,2})\\b"), type);
            add(QStringLiteral("^\\s*\\w+:"), keyword);
        } else if (l == u"json" || l == u"yaml" || l == u"yml" || l == u"toml" || l == u"ini") {
            add(QStringLiteral("^\\s*[\\w.-]+\\s*(?=[:=])|\"[^\"]*\"(?=\\s*:)"), type);
            add(words("true false null yes no on off"), keyword);
        }
        add(QStringLiteral("\\b(?:0[xX][0-9A-Fa-f]+|0[bB][01]+|\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?)[uUlLfF]*\\b"), number);
        add(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.)*'"), string);
        if (hashComments)
            add(QStringLiteral("(?:^|\\s)#.*$"), comment);
        else if (l == u"sql")
            add(QStringLiteral("--.*$"), comment);
        else if (l == u"asm" || l == u"s" || l == u"nasm")
            add(QStringLiteral("[;@].*$|//.*$"), comment);
        else if (l == u"ini")
            add(QStringLiteral("^\\s*[;#].*$"), comment);
        if (m_blockComments)
            add(QStringLiteral("//.*$"), comment);
        m_comment = comment;
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const auto &[re, f] : std::as_const(m_rules)) {
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                setFormat(int(m.capturedStart()), int(m.capturedLength()), f);
            }
        }
        if (!m_blockComments)
            return;
        // /* … */ across lines: state 1 = inside a comment at the end of the line.
        setCurrentBlockState(0);
        qsizetype start = previousBlockState() == 1 ? 0 : text.indexOf(QLatin1String("/*"));
        while (start >= 0) {
            const qsizetype end = text.indexOf(QLatin1String("*/"), start + (previousBlockState() == 1 && start == 0 ? 0 : 2));
            const qsizetype length = end < 0 ? text.size() - start : end - start + 2;
            if (end < 0)
                setCurrentBlockState(1);
            setFormat(int(start), int(length), m_comment);
            start = end < 0 ? -1 : text.indexOf(QLatin1String("/*"), start + length);
        }
    }

private:
    QList<std::pair<QRegularExpression, QTextCharFormat>> m_rules;
    QTextCharFormat m_comment;
    bool m_blockComments = false;
};

CodeHighlighter::CodeHighlighter(QObject *parent)
    : QObject(parent)
{
}

CodeHighlighter::~CodeHighlighter() = default; // the highlighter belongs to the document

void CodeHighlighter::setDocument(QQuickTextDocument *document)
{
    if (m_document == document)
        return;
    m_document = document;
    attach();
    emit changed();
}

void CodeHighlighter::setLanguage(const QString &language)
{
    if (m_language == language)
        return;
    m_language = language;
    attach();
    emit changed();
}

void CodeHighlighter::attach()
{
    delete m_highlighter.data();
    if (m_document && m_document->textDocument())
        m_highlighter = new Highlighter(m_document->textDocument(), m_language);
}
