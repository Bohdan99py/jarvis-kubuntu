#include "code_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <functional>

#include "memory_text.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

double rounded(double v) { return std::round(v * 10.0) / 10.0; }

QString cleanName(const QString &s, int max)
{
    QString out = s.simplified().left(max);
    out.remove(QRegularExpression(u"[\\x00-\\x1f\\x7f<>]"_s));
    return out;
}

// Keeps the `max` entries with the largest value of `field`.
void keepLargest(QJsonObject &map, const QString &field, int max)
{
    while (map.size() > max) {
        QString weakest;
        double min = 1e300;
        for (auto it = map.begin(); it != map.end(); ++it) {
            const double v = it.value().toObject().value(field).toDouble();
            if (v < min) {
                min = v;
                weakest = it.key();
            }
        }
        map.remove(weakest);
    }
}

} // namespace

QString CodeStore::path()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/code.json"_s;
}

QJsonObject CodeStore::load() const
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly) || f.size() > 16 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool CodeStore::save(const QJsonObject &root) const
{
    QDir dir(QFileInfo(path()).absolutePath());
    if (!dir.mkpath(u"."_s))
        return false;
    QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path());
    if (!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    QJsonObject out = root;
    out[u"version"_s] = 1;
    const QByteArray data = QJsonDocument(out).toJson(QJsonDocument::Compact);
    return f.write(data) == data.size() && f.commit();
}

QString CodeStore::languageName(const QString &id)
{
    static const QMap<QString, QString> names = {
        {u"cpp"_s, u"C++"_s}, {u"c"_s, u"C"_s}, {u"python"_s, u"Python"_s}, {u"javascript"_s, u"JavaScript"_s},
        {u"typescript"_s, u"TypeScript"_s}, {u"javascriptreact"_s, u"React (JSX)"_s},
        {u"typescriptreact"_s, u"React (TSX)"_s}, {u"qml"_s, u"QML"_s}, {u"cmake"_s, u"CMake"_s},
        {u"rust"_s, u"Rust"_s}, {u"go"_s, u"Go"_s}, {u"java"_s, u"Java"_s}, {u"kotlin"_s, u"Kotlin"_s},
        {u"csharp"_s, u"C#"_s}, {u"shellscript"_s, u"Shell"_s}, {u"html"_s, u"HTML"_s}, {u"css"_s, u"CSS"_s},
        {u"json"_s, u"JSON"_s}, {u"yaml"_s, u"YAML"_s}, {u"markdown"_s, u"Markdown"_s}, {u"sql"_s, u"SQL"_s},
        {u"php"_s, u"PHP"_s}, {u"ruby"_s, u"Ruby"_s}, {u"lua"_s, u"Lua"_s}, {u"dart"_s, u"Dart"_s},
        {u"swift"_s, u"Swift"_s}, {u"arduino"_s, u"Arduino"_s}, {u"glsl"_s, u"GLSL"_s},
    };
    const auto it = names.constFind(id);
    if (it != names.cend())
        return *it;
    QString n = id;
    if (!n.isEmpty())
        n[0] = n.at(0).toUpper();
    return n;
}

QString CodeStore::errorSignature(const QString &message)
{
    // "'foo' was not declared in this scope" and "'bar' was not declared…" are one lesson.
    QString s = message.toLower().simplified().left(400);
    s.replace(QRegularExpression(u"[‘'\"`][^‘’'\"`]{0,80}[’'\"`]"_s), u"'…'"_s);
    s.replace(QRegularExpression(u"\\b0x[0-9a-f]+\\b|\\b\\d+\\b"_s), u"N"_s);
    s.replace(QRegularExpression(u"(?:/[\\w.\\-]+)+"_s), u"<path>"_s);
    return s.simplified().left(200);
}

void CodeStore::activity(const QString &languageId, const QString &projectName, qint64 nowMs)
{
    const QString language = cleanName(languageId, 40).toLower();
    const QString project = cleanName(projectName, 80);
    QJsonObject root = load();
    if (!m_lastLanguage.isEmpty() && m_lastBeat > 0 && nowMs > m_lastBeat) {
        const double seconds = double(std::min(nowMs - m_lastBeat, kHeartbeatGapMs)) / 1000.0;
        QJsonObject languages = root.value(u"languages"_s).toObject();
        QJsonObject l = languages.value(m_lastLanguage).toObject();
        l[u"seconds"_s] = rounded(l.value(u"seconds"_s).toDouble() + seconds);
        l[u"last"_s] = double(nowMs);
        languages[m_lastLanguage] = l;
        keepLargest(languages, u"seconds"_s, kMaxLanguages);
        root[u"languages"_s] = languages;
        if (!m_lastProject.isEmpty()) {
            QJsonObject projects = root.value(u"projects"_s).toObject();
            QJsonObject p = projects.value(m_lastProject).toObject();
            p[u"seconds"_s] = rounded(p.value(u"seconds"_s).toDouble() + seconds);
            QJsonObject pl = p.value(u"languages"_s).toObject();
            pl[m_lastLanguage] = rounded(pl.value(m_lastLanguage).toDouble() + seconds);
            p[u"languages"_s] = pl;
            p[u"last"_s] = double(nowMs);
            projects[m_lastProject] = p;
            keepLargest(projects, u"last"_s, kMaxProjects);
            root[u"projects"_s] = projects;
        }
        save(root);
    }
    m_lastLanguage = language;
    m_lastProject = project;
    m_lastBeat = nowMs;
}

void CodeStore::workspace(const QString &projectName, const QStringList &languages, const QStringList &frameworks,
                          qint64 nowMs)
{
    const QString project = cleanName(projectName, 80);
    if (project.isEmpty())
        return;
    QJsonObject root = load();
    QJsonObject projects = root.value(u"projects"_s).toObject();
    QJsonObject p = projects.value(project).toObject();
    QJsonArray fw;
    for (const QString &f : frameworks.mid(0, 20))
        if (const QString name = cleanName(f, 40); !name.isEmpty())
            fw.append(name);
    p[u"frameworks"_s] = fw;
    QJsonArray langs;
    for (const QString &l : languages.mid(0, 10))
        langs.append(cleanName(l, 40).toLower());
    p[u"fileLanguages"_s] = langs;
    p[u"last"_s] = double(nowMs);
    projects[project] = p;
    keepLargest(projects, u"last"_s, kMaxProjects);
    root[u"projects"_s] = projects;
    root[u"current"_s] = project;
    save(root);
}

void CodeStore::diagnostic(const QString &languageId, const QString &projectName, const QString &message, qint64 nowMs)
{
    const QString signature = errorSignature(message);
    if (signature.size() < 4 || text::looksSensitive(message))
        return;
    QJsonObject root = load();
    QJsonObject errors = root.value(u"errors"_s).toObject();
    QJsonObject e = errors.value(signature).toObject();
    e[u"language"_s] = cleanName(languageId, 40).toLower();
    e[u"project"_s] = cleanName(projectName, 80);
    e[u"message"_s] = cleanName(message, 200);
    e[u"count"_s] = e.value(u"count"_s).toInt() + 1;
    e[u"last"_s] = double(nowMs);
    errors[signature] = e;
    keepLargest(errors, u"last"_s, kMaxErrors);
    root[u"errors"_s] = errors;
    save(root);
}

QString CodeStore::addLesson(const QString &languageId, const QString &problemText, const QString &solutionText,
                             const QString &source, qint64 nowMs)
{
    const QString problem = problemText.trimmed().left(500);
    const QString solution = solutionText.trimmed().left(3000);
    if (problem.size() < 3 || solution.size() < 3 || text::looksSensitive(problem) || text::looksSensitive(solution))
        return {};
    const QString language = cleanName(languageId, 40).toLower();
    QJsonObject root = load();
    QJsonArray lessons = root.value(u"lessons"_s).toArray();
    // Same problem in the same language: the newer solution replaces the old one.
    const QSet<QString> key = text::stems(problem);
    for (int i = 0; i < lessons.size(); ++i) {
        QJsonObject l = lessons.at(i).toObject();
        if (l.value(u"language"_s).toString() == language
            && (l.value(u"problem"_s).toString() == problem
                || text::overlap(key, text::stems(l.value(u"problem"_s).toString())) >= 0.9)) {
            l[u"solution"_s] = solution;
            l[u"source"_s] = source;
            l[u"last"_s] = double(nowMs);
            lessons[i] = l;
            root[u"lessons"_s] = lessons;
            save(root);
            return l.value(u"id"_s).toString();
        }
    }
    if (lessons.size() >= kMaxLessons) {
        int victim = 0;
        double worst = 1e300;
        for (int i = 0; i < lessons.size(); ++i) {
            const QJsonObject l = lessons.at(i).toObject();
            const double score = l.value(u"uses"_s).toDouble() + 3 * l.value(u"good"_s).toDouble()
                                 + l.value(u"last"_s).toDouble() / 1e13;
            if (score < worst) {
                worst = score;
                victim = i;
            }
        }
        lessons.removeAt(victim);
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    lessons.append(QJsonObject{{u"id"_s, id}, {u"language"_s, language}, {u"problem"_s, problem},
                               {u"solution"_s, solution}, {u"source"_s, source}, {u"uses"_s, 0},
                               {u"good"_s, 0}, {u"created"_s, double(nowMs)}, {u"last"_s, double(nowMs)}});
    root[u"lessons"_s] = lessons;
    return save(root) ? id : QString();
}

bool CodeStore::removeLesson(const QString &id)
{
    QJsonObject root = load();
    QJsonArray lessons = root.value(u"lessons"_s).toArray();
    for (int i = 0; i < lessons.size(); ++i)
        if (lessons.at(i).toObject().value(u"id"_s).toString() == id) {
            lessons.removeAt(i);
            root[u"lessons"_s] = lessons;
            return save(root);
        }
    return false;
}

void CodeStore::rateLesson(const QString &id, bool good)
{
    if (!good) {
        removeLesson(id); // a solution that did not help must not come back
        return;
    }
    QJsonObject root = load();
    QJsonArray lessons = root.value(u"lessons"_s).toArray();
    for (int i = 0; i < lessons.size(); ++i) {
        QJsonObject l = lessons.at(i).toObject();
        if (l.value(u"id"_s).toString() == id) {
            l[u"good"_s] = l.value(u"good"_s).toInt() + 1;
            lessons[i] = l;
            root[u"lessons"_s] = lessons;
            save(root);
            return;
        }
    }
}

QList<CodeStore::Lesson> CodeStore::similar(const QString &problem, const QString &languageId, double minScore,
                                            int limit) const
{
    QList<Lesson> out;
    const QSet<QString> wanted = text::stems(problem + u' ' + errorSignature(problem));
    if (wanted.isEmpty())
        return out;
    const QString language = languageId.toLower();
    for (const auto &v : load().value(u"lessons"_s).toArray()) {
        const QJsonObject l = v.toObject();
        const QString lessonLanguage = l.value(u"language"_s).toString();
        if (!language.isEmpty() && !lessonLanguage.isEmpty() && lessonLanguage != language)
            continue;
        const QString p = l.value(u"problem"_s).toString();
        double score = text::overlap(wanted, text::stems(p + u' ' + errorSignature(p)));
        score += 0.05 * std::min(4, l.value(u"good"_s).toInt());
        if (score >= minScore)
            out.append({l.value(u"id"_s).toString(), lessonLanguage, p, l.value(u"solution"_s).toString(),
                        l.value(u"source"_s).toString(), score});
    }
    std::sort(out.begin(), out.end(), [](const Lesson &a, const Lesson &b) { return a.score > b.score; });
    return out.mid(0, limit);
}

QStringList CodeStore::languagesUsed(double minSeconds) const
{
    QList<std::pair<double, QString>> list;
    const QJsonObject languages = load().value(u"languages"_s).toObject();
    for (auto it = languages.begin(); it != languages.end(); ++it) {
        static const QSet<QString> notLanguages = {u"plaintext"_s, u"json"_s, u"jsonc"_s, u"markdown"_s,
                                                   u"log"_s, u"ignore"_s, u"yaml"_s, u"properties"_s};
        const double s = it.value().toObject().value(u"seconds"_s).toDouble();
        if (s >= minSeconds && !notLanguages.contains(it.key()))
            list.append({s, it.key()});
    }
    std::sort(list.begin(), list.end(), std::greater<>());
    QStringList out;
    for (const auto &[seconds, id] : list)
        out.append(id);
    return out;
}

QStringList CodeStore::frameworks(const QString &project) const
{
    QStringList out;
    for (const auto &v : load().value(u"projects"_s).toObject().value(project).toObject().value(u"frameworks"_s).toArray())
        out.append(v.toString());
    return out;
}

QString CodeStore::currentProject() const
{
    return load().value(u"current"_s).toString();
}

int CodeStore::lessonsSince(qint64 sinceMs) const
{
    int n = 0;
    for (const auto &v : load().value(u"lessons"_s).toArray())
        n += v.toObject().value(u"created"_s).toDouble() >= double(sinceMs) ? 1 : 0;
    return n;
}

QString CodeStore::profileForPrompt() const
{
    const QJsonObject root = load();
    QStringList langs;
    const QJsonObject languages = root.value(u"languages"_s).toObject();
    for (const QString &id : languagesUsed(15 * 60).mid(0, 6))
        langs.append(languageName(id) + u" ("_s
                     + QString::number(int(languages.value(id).toObject().value(u"seconds"_s).toDouble() / 3600.0 + 0.5))
                     + u" h)"_s);
    QString out;
    if (!langs.isEmpty())
        out += u"Languages the user works in: "_s + langs.join(u", "_s) + u".\n"_s;
    const QString project = root.value(u"current"_s).toString();
    if (!project.isEmpty()) {
        out += u"Current project: "_s + project;
        const QStringList fw = frameworks(project);
        if (!fw.isEmpty())
            out += u" (uses "_s + fw.join(u", "_s) + u')';
        out += u".\n"_s;
    }
    return out;
}

QJsonObject CodeStore::snapshot() const
{
    const QJsonObject root = load();
    QJsonArray languages;
    const QJsonObject langMap = root.value(u"languages"_s).toObject();
    for (const QString &id : languagesUsed(60).mid(0, 8))
        languages.append(QJsonObject{{u"id"_s, id}, {u"name"_s, languageName(id)},
                                     {u"seconds"_s, langMap.value(id).toObject().value(u"seconds"_s)}});

    QList<std::pair<double, QString>> projectOrder;
    const QJsonObject projects = root.value(u"projects"_s).toObject();
    for (auto it = projects.begin(); it != projects.end(); ++it)
        projectOrder.append({it.value().toObject().value(u"last"_s).toDouble(), it.key()});
    std::sort(projectOrder.begin(), projectOrder.end(), std::greater<>());
    QJsonArray projectList;
    for (int i = 0; i < projectOrder.size() && i < 6; ++i) {
        const QJsonObject p = projects.value(projectOrder.at(i).second).toObject();
        projectList.append(QJsonObject{{u"name"_s, projectOrder.at(i).second}, {u"seconds"_s, p.value(u"seconds"_s)},
                                       {u"frameworks"_s, p.value(u"frameworks"_s)}});
    }

    QList<std::pair<int, QJsonObject>> errorOrder;
    const QJsonObject errors = root.value(u"errors"_s).toObject();
    for (auto it = errors.begin(); it != errors.end(); ++it)
        errorOrder.append({it.value().toObject().value(u"count"_s).toInt(), it.value().toObject()});
    std::sort(errorOrder.begin(), errorOrder.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QJsonArray errorList;
    for (int i = 0; i < errorOrder.size() && i < 5; ++i)
        errorList.append(errorOrder.at(i).second);

    QJsonArray lessons;
    const QJsonArray stored = root.value(u"lessons"_s).toArray();
    for (int i = stored.size() - 1; i >= 0 && lessons.size() < 30; --i) {
        QJsonObject l = stored.at(i).toObject();
        l[u"solution"_s] = l.value(u"solution"_s).toString().left(400);
        lessons.append(l);
    }
    return QJsonObject{{u"languages"_s, languages}, {u"projects"_s, projectList}, {u"errors"_s, errorList},
                       {u"lessons"_s, lessons}, {u"lessonCount"_s, stored.size()}};
}

void CodeStore::clear()
{
    save(QJsonObject{});
    m_lastLanguage.clear();
    m_lastProject.clear();
    m_lastBeat = 0;
}

} // namespace jarvis
