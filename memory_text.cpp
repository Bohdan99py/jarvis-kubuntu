#include "memory_text.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace jarvis::text {
namespace {

const QSet<QString> &stopWords()
{
    static const QSet<QString> words = {
        // Russian
        u"что"_s, u"как"_s, u"это"_s, u"для"_s, u"или"_s, u"так"_s, u"его"_s, u"она"_s, u"они"_s,
        u"мне"_s, u"меня"_s, u"мой"_s, u"моя"_s, u"моё"_s, u"мое"_s, u"мои"_s, u"тебя"_s, u"тебе"_s,
        u"твой"_s, u"вас"_s, u"вам"_s, u"нас"_s, u"нам"_s, u"был"_s, u"была"_s, u"было"_s, u"будет"_s,
        u"есть"_s, u"нет"_s, u"уже"_s, u"еще"_s, u"ещё"_s, u"там"_s, u"тут"_s, u"где"_s, u"когда"_s,
        u"чем"_s, u"чтобы"_s, u"если"_s, u"только"_s, u"очень"_s, u"можно"_s, u"нужно"_s, u"надо"_s,
        u"все"_s, u"всё"_s, u"весь"_s, u"вот"_s, u"при"_s, u"про"_s, u"над"_s, u"под"_s, u"без"_s,
        u"кто"_s, u"какой"_s, u"какая"_s, u"какие"_s, u"который"_s, u"сейчас"_s, u"тоже"_s,
        u"также"_s, u"себя"_s, u"свой"_s, u"этот"_s, u"эта"_s, u"эти"_s, u"того"_s, u"тот"_s,
        u"пожалуйста"_s, u"спасибо"_s, u"привет"_s, u"jarvis"_s, u"джарвис"_s, u"скажи"_s,
        u"расскажи"_s, u"покажи"_s, u"знаешь"_s, u"можешь"_s, u"давай"_s,
        // English
        u"the"_s, u"and"_s, u"for"_s, u"are"_s, u"but"_s, u"not"_s, u"you"_s, u"your"_s, u"with"_s,
        u"this"_s, u"that"_s, u"what"_s, u"how"_s, u"why"_s, u"who"_s, u"when"_s, u"where"_s,
        u"which"_s, u"was"_s, u"were"_s, u"been"_s, u"have"_s, u"has"_s, u"had"_s, u"does"_s,
        u"did"_s, u"can"_s, u"could"_s, u"would"_s, u"should"_s, u"will"_s, u"about"_s, u"into"_s,
        u"just"_s, u"some"_s, u"any"_s, u"there"_s, u"their"_s, u"they"_s, u"them"_s, u"then"_s,
        u"than"_s, u"its"_s, u"from"_s, u"please"_s, u"thanks"_s, u"hello"_s, u"tell"_s,
        u"show"_s, u"know"_s, u"also"_s, u"very"_s, u"mine"_s, u"our"_s, u"his"_s, u"her"_s,
    };
    return words;
}

} // namespace

QString normalize(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (QChar c : text.toLower()) {
        if (c == u'ё')
            c = u'е';
        out += c.isLetterOrNumber() ? c : QChar(u' ');
    }
    return out.simplified();
}

QStringList contentWords(const QString &text, int limit)
{
    QStringList out;
    QSet<QString> seen;
    const QStringList words = normalize(text).split(u' ', Qt::SkipEmptyParts);
    for (const QString &w : words) {
        if (out.size() >= limit)
            break;
        if (w.size() <= 2 || stopWords().contains(w) || seen.contains(w))
            continue;
        bool digits = true;
        for (const QChar c : w)
            digits = digits && c.isDigit();
        if (digits)
            continue;
        seen.insert(w);
        out.append(w);
    }
    return out;
}

QSet<QString> stems(const QString &text)
{
    QSet<QString> out;
    for (const QString &w : contentWords(text, 64))
        out.insert(w.left(5));
    return out;
}

double overlap(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0.0;
    int common = 0;
    for (const QString &s : a)
        common += b.contains(s) ? 1 : 0;
    return double(common) / double(a.size() + b.size() - common);
}

bool looksSensitive(const QString &text)
{
    static const QRegularExpression patterns(
        u"(sk-ant-|ghp_|github_pat_|-----BEGIN|\\b(?:\\d[ -]?){13,19}\\b|"
        u"\\b(?:password|passwd|пароль|парол[яеи]|pin[- ]?code|пин[- ]?код|cvv|cvc|iban)\\b)"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return patterns.match(text).hasMatch();
}

} // namespace jarvis::text
