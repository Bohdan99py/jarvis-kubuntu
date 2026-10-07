#include "knowledge_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <functional>

#include "memory_text.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

struct Pattern
{
    QRegularExpression re;
    QString slot;
};

QRegularExpression rx(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption
                                           | QRegularExpression::UseUnicodePropertiesOption);
}

// Order matters: the more specific phrase must come first
// ("я работаю над" before "я работаю", "i don't like" before "i like").
const QList<Pattern> &patterns()
{
    static const QList<Pattern> list = {
        {rx(u"(?:меня зовут|мо[её] имя|зови меня|называй меня)\\s+([\\p{L}-]{2,30})"_s), u"name"_s},
        {rx(u"(?:my name is|call me|i am called|i'm called)\\s+([\\p{L}-]{2,30})"_s), u"name"_s},
        {rx(u"(?:я живу (?:в|во)|я из|живу (?:в|во))\\s+([\\p{L}][\\p{L}\\- ]{1,40})"_s), u"location"_s},
        {rx(u"(?:i live in|i'm from|i am from|i'm based in|i am based in)\\s+([\\p{L}][\\p{L}\\- ]{1,40})"_s), u"location"_s},
        {rx(u"(?:я работаю над|работаю над|мой проект\\s*[—:-]?|я разрабатываю|я делаю проект)\\s+(.{3,80})"_s), u"project"_s},
        {rx(u"(?:i'm working on|i am working on|my project is|i'm building|i am building|i'm developing|i am developing)\\s+(.{3,80})"_s), u"project"_s},
        // "я работаю программистом": only the instrumental case, so "я работаю дома" teaches nothing.
        {rx(u"я работаю\\s+([\\p{L}-]{3,30}(?:ом|ем|ём|ой|ей))(?![\\p{L}])"_s), u"occupation"_s},
        {rx(u"(?:по профессии я|я по профессии|моя профессия\\s*[—:-]?)\\s+([\\p{L}][\\p{L}\\- ]{2,50})"_s), u"occupation"_s},
        {rx(u"(?:i work as|my job is|my profession is)\\s+(?:an?\\s+)?(.{3,50})"_s), u"occupation"_s},
        {rx(u"(?:i am|i'm)\\s+an?\\s+((?:[\\p{L}+#-]+\\s+)?(?:developer|engineer|programmer|designer|student|teacher|doctor|artist|writer|manager|scientist|researcher|musician|electrician|technician|analyst|administrator|admin))\\b"_s), u"occupation"_s},
        {rx(u"(?:мой день рождения|я родился|я родилась)\\s+(.{3,40})"_s), u"birthday"_s},
        {rx(u"(?:my birthday is|i was born on|i was born in)\\s+(.{3,40})"_s), u"birthday"_s},
        {rx(u"(?:я не люблю|я ненавижу|мне не нравится|мне не нравятся)\\s+(.{3,60})"_s), u"dislikes"_s},
        {rx(u"(?:i don't like|i do not like|i hate|i dislike)\\s+(.{3,60})"_s), u"dislikes"_s},
        {rx(u"(?:я люблю|я обожаю|мне нравится|мне нравятся)\\s+(.{3,60})"_s), u"likes"_s},
        {rx(u"(?:i like|i love|i enjoy|i prefer)\\s+(.{3,60})"_s), u"likes"_s},
        {rx(u"(?:я пишу на|я программирую на|пишу код на)\\s+([\\p{L}+#.]{1,20})"_s), u"skill"_s},
        {rx(u"(?:i code in|i program in|i write code in|i write in)\\s+([\\p{L}+#.]{1,20})"_s), u"skill"_s},
        {rx(u"(?:я использую|я пользуюсь|у меня стоит|у меня установлен[аоы]?)\\s+(.{2,60})"_s), u"uses"_s},
        {rx(u"(?:i use|i'm using|i am using)\\s+(.{2,60})"_s), u"uses"_s},
    };
    return list;
}

bool singleValued(const QString &slot)
{
    static const QSet<QString> single = {u"name"_s, u"location"_s, u"occupation"_s,
                                        u"project"_s, u"birthday"_s, u"goal"_s, u"active_hours"_s};
    return single.contains(slot);
}

// "Kate, but sometimes vim. Also…" -> "Kate"; "the terminal for builds" -> "terminal".
// Returns an empty string for values that are only a pronoun ("I use it").
QString cleanValue(QString value, bool shortValue)
{
    static const QRegularExpression stop(
        u"[.!?;\\n]|,\\s|\\s(?:но|а|потому что|because|but|and then)\\s"_s,
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression purpose(
        u"\\s(?:to|for|when|while|чтобы|для|когда|пока)\\s"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression article(u"^(?:the|a|an|to)\\s+"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pronoun(
        u"^(?:it|this|that|them|these|those|him|her|you|его|её|ее|это|эту|этот|эти|их|там|тут|тебя|вас)(?:\\s|$)"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    auto cut = [&value](const QRegularExpression &re) {
        const auto m = re.match(value);
        if (m.hasMatch())
            value.truncate(m.capturedStart());
    };
    cut(stop);
    if (shortValue)
        cut(purpose);
    value = value.simplified();
    value.remove(article);
    if (pronoun.match(value).hasMatch())
        return {};
    while (!value.isEmpty() && !value.back().isLetterOrNumber() && value.back() != u'+' && value.back() != u'#')
        value.chop(1);
    return value.left(80);
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

QString linkKey(const QString &a, const QString &b)
{
    return a < b ? a + QChar(0x1f) + b : b + QChar(0x1f) + a;
}

template <typename Map>
void pruneSmallest(Map &map, int max)
{
    if (map.size() <= max)
        return;
    QList<std::pair<double, QString>> order;
    for (auto it = map.begin(); it != map.end(); ++it)
        order.append({it.value().toDouble(), it.key()});
    std::sort(order.begin(), order.end());
    for (int i = 0; i < order.size() - max; ++i)
        map.remove(order.at(i).second);
}

} // namespace

QString KnowledgeStore::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/knowledge.json"_s;
}

QJsonObject KnowledgeStore::load() const
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool KnowledgeStore::save(const QJsonObject &root) const
{
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(u"."_s))
        return false;
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    return f.write(data) == data.size() && f.commit();
}

void KnowledgeStore::upsertFact(QJsonArray &facts, const QString &slot, const QString &value,
                                const QString &source, double confidence)
{
    const QString key = text::normalize(value);
    for (int i = 0; i < facts.size(); ++i) {
        QJsonObject f = facts.at(i).toObject();
        if (f.value(u"slot"_s).toString() != slot)
            continue;
        const bool same = text::normalize(f.value(u"value"_s).toString()) == key;
        if (!same && !singleValued(slot))
            continue;
        // Same fact said again: reinforce. New value for a single-valued slot: replace.
        f[u"confidence"_s] = same ? std::min(1.0, f.value(u"confidence"_s).toDouble() + 0.1)
                                  : std::max(confidence, f.value(u"confidence"_s).toDouble() * 0.5 + 0.3);
        f[u"hits"_s] = same ? f.value(u"hits"_s).toInt() + 1 : 1;
        f[u"value"_s] = value;
        f[u"source"_s] = source;
        f[u"seen"_s] = nowMs();
        facts[i] = f;
        return;
    }
    if (facts.size() >= kMaxFacts) {
        // Evict the weakest, oldest auto-learned fact; manual notes stay.
        int victim = -1;
        double worst = 2.0;
        for (int i = 0; i < facts.size(); ++i) {
            const QJsonObject f = facts.at(i).toObject();
            if (f.value(u"source"_s).toString() == u"manual")
                continue;
            const double score = f.value(u"confidence"_s).toDouble() + f.value(u"seen"_s).toDouble() / 1e15;
            if (score < worst) {
                worst = score;
                victim = i;
            }
        }
        if (victim < 0)
            return;
        facts.removeAt(victim);
    }
    facts.append(QJsonObject{
        {u"id"_s, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)},
        {u"slot"_s, slot},
        {u"value"_s, value},
        {u"source"_s, source},
        {u"confidence"_s, confidence},
        {u"hits"_s, 1},
        {u"created"_s, double(nowMs())},
        {u"seen"_s, double(nowMs())},
    });
}

QList<KnowledgeStore::Learned> KnowledgeStore::observe(const QString &message, bool learnFacts)
{
    QList<Learned> learned;
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty() || text::looksSensitive(trimmed))
        return learned;

    QJsonObject root = load();
    QJsonArray facts = root.value(u"facts"_s).toArray();

    const bool question = trimmed.endsWith(u'?');
    if (learnFacts && !question) {
        // Same length as `trimmed`, so match offsets map back to the user's spelling.
        QString lower = trimmed;
        lower.replace(u'’', u'\'');
        QSet<QString> filled;
        for (const Pattern &p : patterns()) {
            if (filled.contains(p.slot))
                continue;
            const auto m = p.re.match(lower);
            if (!m.hasMatch())
                continue;
            // Keep the user's own capitalisation: same offsets in the original text.
            const bool shortValue = p.slot == u"likes" || p.slot == u"dislikes" || p.slot == u"uses" || p.slot == u"skill";
            QString value = cleanValue(trimmed.mid(m.capturedStart(1), m.capturedLength(1)), shortValue);
            if (!check(value).isEmpty())
                continue;
            if (p.slot == u"name" && value.front().isLower())
                value[0] = value.front().toUpper();
            upsertFact(facts, p.slot, value, u"dialog"_s, 0.6);
            learned.append({p.slot, value});
            filled.insert(p.slot);
        }
    }

    // Forgetting: weak facts learned automatically that nothing confirmed for
    // 90 days fade away; reinforced facts (confidence ≥ 0.7) and notes stay.
    static const QSet<QString> automatic = {u"dialog"_s, u"reflection"_s, u"activity"_s, u"vscode"_s, u"claude"_s};
    constexpr double kFadeMs = 90.0 * 24 * 3600 * 1000;
    for (int i = facts.size() - 1; i >= 0; --i) {
        const QJsonObject f = facts.at(i).toObject();
        if (automatic.contains(f.value(u"source"_s).toString()) && f.value(u"confidence"_s).toDouble() < 0.7
            && double(nowMs()) - f.value(u"seen"_s).toDouble() > kFadeMs)
            facts.removeAt(i);
    }

    // Topics: what the user talks about, as word counts and co-occurrence.
    QJsonObject topics = root.value(u"topics"_s).toObject();
    QJsonObject links = root.value(u"links"_s).toObject();
    const QStringList words = text::contentWords(trimmed, 8);
    for (const QString &w : words)
        topics[w] = topics.value(w).toInt() + 1;
    for (int i = 0; i < words.size(); ++i)
        for (int j = i + 1; j < words.size(); ++j) {
            const QString k = linkKey(words.at(i), words.at(j));
            links[k] = links.value(k).toInt() + 1;
        }
    pruneSmallest(topics, kMaxTopics);
    // Drop links whose words were pruned, then cap.
    for (auto it = links.begin(); it != links.end();) {
        const QStringList pair = it.key().split(QChar(0x1f));
        if (pair.size() != 2 || !topics.contains(pair.at(0)) || !topics.contains(pair.at(1)))
            it = links.erase(it);
        else
            ++it;
    }
    pruneSmallest(links, kMaxLinks);

    root[u"version"_s] = 1;
    root[u"facts"_s] = facts;
    root[u"topics"_s] = topics;
    root[u"links"_s] = links;
    save(root);
    return learned;
}

bool KnowledgeStore::knownSlot(const QString &slot)
{
    static const QSet<QString> slots_ = {u"name"_s, u"location"_s, u"occupation"_s, u"project"_s,
                                         u"birthday"_s, u"likes"_s, u"dislikes"_s, u"skill"_s, u"uses"_s,
                                         u"goal"_s, u"active_hours"_s, u"note"_s};
    return slots_.contains(slot);
}

QString KnowledgeStore::check(const QString &value)
{
    if (value.size() < 2)
        return u"short"_s;
    if (value.size() > kMaxFactChars)
        return u"long"_s;
    if (text::looksSensitive(value))
        return u"sensitive"_s;
    // Facts go back into Claude's system prompt: nothing that reads like markup
    // or an instruction may be stored, whoever proposed it.
    static const QRegularExpression injection(
        u"[<>{}]|ignore (?:all|any|previous|the above)|system prompt|you are now|assistant must|"
        u"игнорируй|системн\\w* (?:промпт|инструкц)|ты теперь"_s,
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (injection.match(value).hasMatch())
        return u"unsafe"_s;
    return {};
}

QString KnowledgeStore::learn(const QString &slot, const QString &input, const QString &source, double confidence)
{
    QString value = input.simplified();
    value.remove(QRegularExpression(u"[\\x00-\\x1f\\x7f]"_s));
    if (!knownSlot(slot))
        return u"slot"_s;
    if (const QString error = check(value); !error.isEmpty())
        return error;
    QJsonObject root = load();
    QJsonArray facts = root.value(u"facts"_s).toArray();
    upsertFact(facts, slot, value, source, confidence);
    root[u"version"_s] = 1;
    root[u"facts"_s] = facts;
    return save(root) ? QString() : u"io"_s;
}

QString KnowledgeStore::remember(const QString &input, const QString &source)
{
    if (input.simplified().size() < 3)
        return u"short"_s;
    return learn(u"note"_s, input, source, source == u"manual" ? 1.0 : 0.7);
}

bool KnowledgeStore::hasSlot(const QString &slot) const
{
    for (const auto &v : load().value(u"facts"_s).toArray())
        if (v.toObject().value(u"slot"_s).toString() == slot)
            return true;
    return false;
}

QJsonObject KnowledgeStore::curiosityState() const
{
    return load().value(u"curiosity"_s).toObject();
}

void KnowledgeStore::setCuriosityState(const QJsonObject &state)
{
    QJsonObject root = load();
    root[u"version"_s] = 1;
    root[u"curiosity"_s] = state;
    save(root);
}

int KnowledgeStore::forget(const QString &query, int maxMatches)
{
    QJsonObject root = load();
    QJsonArray facts = root.value(u"facts"_s).toArray();
    const QSet<QString> wanted = text::stems(query);
    const QString normalized = text::normalize(query);
    QList<int> byId;
    QList<int> byText;
    for (int i = 0; i < facts.size(); ++i) {
        const QJsonObject f = facts.at(i).toObject();
        if (f.value(u"id"_s).toString() == query) {
            byId.append(i);
            continue;
        }
        if (normalized.size() < 3)
            continue;
        const QString value = f.value(u"value"_s).toString();
        const bool words = (u' ' + text::normalize(value) + u' ').contains(u' ' + normalized + u' ');
        if (words || text::overlap(wanted, text::stems(value)) >= 0.5)
            byText.append(i);
    }
    if (byId.isEmpty() && byText.size() > maxMatches)
        return -int(byText.size());
    QList<int> remove = byId.isEmpty() ? byText : byId;
    std::sort(remove.begin(), remove.end(), std::greater<>());
    for (int i : std::as_const(remove))
        facts.removeAt(i);
    if (!remove.isEmpty()) {
        root[u"facts"_s] = facts;
        save(root);
    }
    return int(remove.size());
}

void KnowledgeStore::clear()
{
    save(QJsonObject{{u"version"_s, 1}});
}

QJsonArray KnowledgeStore::facts() const
{
    QJsonArray facts = load().value(u"facts"_s).toArray();
    QList<QJsonObject> list;
    for (const auto &v : facts)
        list.append(v.toObject());
    std::sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(u"seen"_s).toDouble() > b.value(u"seen"_s).toDouble();
    });
    QJsonArray out;
    for (const auto &o : list)
        out.append(o);
    return out;
}

QJsonObject KnowledgeStore::topics() const
{
    return load().value(u"topics"_s).toObject();
}

QString KnowledgeStore::slotLabel(const QString &slot, Lang lang)
{
    const bool ru = lang == Lang::Ru;
    static const QMap<QString, std::pair<QString, QString>> labels = {
        {u"name"_s, {u"Имя"_s, u"Name"_s}},
        {u"location"_s, {u"Город"_s, u"Location"_s}},
        {u"occupation"_s, {u"Работа"_s, u"Occupation"_s}},
        {u"project"_s, {u"Проект"_s, u"Project"_s}},
        {u"birthday"_s, {u"День рождения"_s, u"Birthday"_s}},
        {u"likes"_s, {u"Нравится"_s, u"Likes"_s}},
        {u"dislikes"_s, {u"Не нравится"_s, u"Dislikes"_s}},
        {u"skill"_s, {u"Пишет на"_s, u"Codes in"_s}},
        {u"uses"_s, {u"Использует"_s, u"Uses"_s}},
        {u"goal"_s, {u"Цель"_s, u"Goal"_s}},
        {u"active_hours"_s, {u"Активные часы"_s, u"Active hours"_s}},
        {u"note"_s, {u"Заметка"_s, u"Note"_s}},
    };
    const auto it = labels.constFind(slot);
    if (it == labels.cend())
        return slot;
    return ru ? it->first : it->second;
}

QString KnowledgeStore::profileForPrompt(int maxChars) const
{
    QString out;
    for (const auto &v : facts()) {
        const QJsonObject f = v.toObject();
        // Older files may predate the markup filter: neutralise angle brackets on the way out.
        QString value = f.value(u"value"_s).toString();
        value.replace(u'<', u'‹').replace(u'>', u'›');
        const QString line = u"- "_s + f.value(u"slot"_s).toString() + u": "_s + value + u'\n';
        if (out.size() + line.size() > maxChars)
            break;
        out += line;
    }
    return out;
}

QString KnowledgeStore::describe(Lang lang, int max) const
{
    const bool ru = lang == Lang::Ru;
    const QJsonArray all = facts();
    if (all.isEmpty()) {
        return ru ? u"Пока я ничего о вас не знаю. Расскажите о себе или скажите «запомни, что …»."_s
                  : u"I don't know anything about you yet. Tell me about yourself or say \"remember that …\"."_s;
    }
    QStringList lines;
    for (const auto &v : all) {
        if (lines.size() >= max)
            break;
        const QJsonObject f = v.toObject();
        lines.append(u"• "_s + slotLabel(f.value(u"slot"_s).toString(), lang) + u": "_s + f.value(u"value"_s).toString());
    }
    QString head = ru ? u"Вот что я о вас помню:"_s : u"Here is what I remember about you:"_s;
    QString tail;
    if (all.size() > lines.size())
        tail = ru ? u"\n…и ещё %1. Полный список — на вкладке «Обо мне»."_s.arg(all.size() - lines.size())
                  : u"\n…and %1 more. See the full list in the \"About me\" tab."_s.arg(all.size() - lines.size());
    return head + u'\n' + lines.join(u'\n') + tail;
}

QString KnowledgeStore::graph(const QJsonArray &examples) const
{
    // weight per word and per source kind; links between words of one document.
    QMap<QString, double> weight;
    QMap<QString, QMap<QString, double>> byKind;
    QMap<QString, double> links;
    auto addDocument = [&](const QString &textValue, const QString &kind, double w) {
        QStringList words = text::contentWords(textValue, 20);
        words.sort();
        for (const QString &word : words) {
            weight[word] += w;
            byKind[word][kind] += w;
        }
        for (int i = 0; i < words.size(); ++i)
            for (int j = i + 1; j < words.size(); ++j)
                links[linkKey(words.at(i), words.at(j))] += w;
    };
    for (const auto &v : examples) {
        const QJsonObject o = v.toObject();
        addDocument(o.value(u"question"_s).toString() + u' ' + o.value(u"answer"_s).toString(), u"example"_s, 1.0);
    }
    const QJsonObject root = load();
    const QJsonArray facts = root.value(u"facts"_s).toArray();
    for (const auto &v : facts)
        addDocument(v.toObject().value(u"value"_s).toString(), u"fact"_s, 1.0);
    const QJsonObject topics = root.value(u"topics"_s).toObject();
    for (auto it = topics.begin(); it != topics.end(); ++it) {
        // Dialogue words weigh less than taught examples: a word needs to come up
        // a few times before it outranks deliberate teaching.
        const double w = it.value().toDouble() * 0.5;
        weight[it.key()] += w;
        byKind[it.key()][u"topic"_s] += w;
    }
    const QJsonObject topicLinks = root.value(u"links"_s).toObject();
    for (auto it = topicLinks.begin(); it != topicLinks.end(); ++it)
        links[it.key()] += it.value().toDouble() * 0.5;

    QStringList names = weight.keys();
    std::sort(names.begin(), names.end(), [&](const QString &a, const QString &b) {
        return weight[a] == weight[b] ? a < b : weight[a] > weight[b];
    });
    names = names.mid(0, 48);
    QJsonArray nodes, edges;
    for (const QString &name : names) {
        QString kind;
        double best = -1;
        for (auto it = byKind[name].cbegin(); it != byKind[name].cend(); ++it)
            if (it.value() > best) {
                best = it.value();
                kind = it.key();
            }
        nodes.append(QJsonObject{{u"label"_s, name}, {u"weight"_s, std::max(1, int(weight[name] + 0.5))}, {u"kind"_s, kind}});
    }
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        const QStringList pair = it.key().split(QChar(0x1f));
        if (pair.size() != 2)
            continue;
        const int a = names.indexOf(pair.at(0));
        const int b = names.indexOf(pair.at(1));
        if (a >= 0 && b >= 0)
            edges.append(QJsonObject{{u"source"_s, a}, {u"target"_s, b}, {u"weight"_s, std::max(1, int(it.value() + 0.5))}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {u"nodes"_s, nodes}, {u"edges"_s, edges}, {u"examples"_s, examples.size()},
        {u"facts"_s, facts.size()}, {u"topics"_s, topics.size()}}).toJson(QJsonDocument::Compact));
}

} // namespace jarvis
