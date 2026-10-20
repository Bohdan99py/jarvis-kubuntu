#include "skill_store.h"
#include "calc_engine.h"
#include "memory_text.h"
#include <QSet>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

static void initSkills() { Q_INIT_RESOURCE(bundled_skills); }
namespace {
QString statePath() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/jarvis/skills.ini"; }
QString key(const QString &s) { QString value=s.toLower().simplified(); value.replace(QRegularExpression("[^\\p{L}\\p{N} ]")," "); return value.simplified(); }
QJsonObject read(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size()>jarvis::SkillStore::kMaxBytes) return {};
    const auto bytes=file.readAll();
    return jarvis::SkillStore::validate(bytes).isEmpty() ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
}
}
namespace jarvis {
SkillStore::SkillStore() { initSkills(); }
QString SkillStore::directory() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/jarvis/skills"; }
QString SkillStore::validate(const QByteArray &bytes) {
    if (bytes.size()>kMaxBytes) return QCoreApplication::translate("jarvis", "The skill file is larger than 160 KB.");
    QJsonParseError error; auto doc=QJsonDocument::fromJson(bytes,&error);
    if (error.error!=QJsonParseError::NoError || !doc.isObject()) return QCoreApplication::translate("jarvis", "A skill must be a JSON object.");
    auto o=doc.object();
    static const QRegularExpression id("^[a-z][a-z0-9-]{0,47}$");
    if (o["schema"].toInt()!=1 || !id.match(o["id"].toString()).hasMatch()) return QCoreApplication::translate("jarvis", "schema: 1 and an id of Latin letters, digits and hyphens are required.");
    for (const auto &field : {"name","description","prompt"})
        if (!o[field].isString() || o[field].toString().trimmed().isEmpty() || o[field].toString().size()>(QString(field)=="prompt"?6000:300))
            return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg(field);
    for (const auto &field : {"name_en","description_en"})
        if (o.contains(field) && (!o[field].isString() || o[field].toString().size()>300))
            return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg(field);
    static const QRegularExpression image("^[a-z][a-z0-9-]{0,31}$"), color("^#[0-9a-fA-F]{6}$");
    if (o.contains("image") && !image.match(o["image"].toString()).hasMatch())
        return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg("image");
    if (o.contains("color") && !color.match(o["color"].toString()).hasMatch())
        return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg("color");
    if (o.contains("tools")) {
        if (!o["tools"].isArray() || o["tools"].toArray().size()>30) return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg("tools");
        for (const auto &t:o["tools"].toArray())
            if (Calculators::definition(t.toString(),Lang::En).isEmpty()) return QCoreApplication::translate("jarvis", "Unknown calculator: %1").arg(t.toString().left(40));
    }
    if (o.contains("formulas")) {
        if (!o["formulas"].isArray() || o["formulas"].toArray().size()>100) return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg("formulas");
        for (const auto &v:o["formulas"].toArray()) {
            const auto f=v.toObject();
            if (!f["title"].isString() || !f["formula"].isString() || f["title"].toString().size()>200 || f["formula"].toString().size()>300
                || f["note"].toString().size()>600 || f["note_en"].toString().size()>600 || f["title_en"].toString().size()>200)
                return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg("formulas");
        }
    }
    if (o.contains("examples") && !o["examples"].isArray()) return QCoreApplication::translate("jarvis", "examples must be an array.");
    if (o["examples"].toArray().size()>kMaxExamples) return QCoreApplication::translate("jarvis", "Up to 150 examples per skill.");
    for (const auto &v:o["examples"].toArray()) {
        auto example=v.toObject();
        for (const auto &field:{"question","answer"})
            if (!example[field].isString() || example[field].toString().trimmed().isEmpty() || example[field].toString().size()>4096)
                return QCoreApplication::translate("jarvis", "Each example needs a question and an answer up to 4096 characters.");
        if (example.contains("aliases") && (!example["aliases"].isArray() || example["aliases"].toArray().size()>10))
            return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg("aliases");
        for (const auto &field:{"question_en","answer_en"})
            if (example.contains(field) && (!example[field].isString() || example[field].toString().size()>4096))
                return QCoreApplication::translate("jarvis", "Invalid skill field: %1").arg(field);
    }
    return {};
}
QJsonArray SkillStore::list() const {
    QJsonArray result; QSettings settings(statePath(),QSettings::IniFormat);
    for (const auto &folder : {QString(":/skills"),directory()}) {
        QDir dir(folder);
        for (const auto &file:dir.entryList({"*.json"},QDir::Files,QDir::Name)) {
            auto o=read(dir.filePath(file)); if (o.isEmpty()) continue;
            const QString id=o["id"].toString(); bool duplicate=false;
            for(const auto &v:result) if(v.toObject()["id"].toString()==id) duplicate=true;
            if(duplicate) continue;
            o["builtin"]=folder.startsWith(':');
            o["enabled"]=settings.value(id+"/enabled",folder.startsWith(':')).toBool();
            result.append(o);
        }
    }
    return result;
}
QString SkillStore::setEnabled(const QString &id,bool enabled) {
    bool exists=false; for(const auto &v:list()) if(v.toObject()["id"].toString()==id) exists=true;
    if(!exists) return QCoreApplication::translate("jarvis", "Skill not found.");
    QSettings s(statePath(),QSettings::IniFormat);s.setValue(id+"/enabled",enabled);s.sync();
    return s.status()==QSettings::NoError ? QString() : QCoreApplication::translate("jarvis", "Could not save the skill state.");
}
QString SkillStore::install(const QByteArray &bytes) {
    const auto error=validate(bytes);if(!error.isEmpty()) return error;
    const auto o=QJsonDocument::fromJson(bytes).object();const QString id=o["id"].toString();
    const auto current=list();
    if(current.size()>=50) return QCoreApplication::translate("jarvis", "Limit: 50 skills.");
    for(const auto &v:current) if(v.toObject()["id"].toString()==id) return QCoreApplication::translate("jarvis", "A skill with this id is already installed.");
    if(!QDir().mkpath(directory())) return QCoreApplication::translate("jarvis", "Cannot create the skills folder.");
    QSaveFile file(directory()+"/"+id+".json");if(!file.open(QIODevice::WriteOnly)) return file.errorString();
    file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    if(file.write(bytes)!=bytes.size() || !file.commit()) return file.errorString();
    // Imported skills start disabled so the user can review before enabling.
    return setEnabled(id,false);
}
QString SkillStore::prompt() const {
    QString text;
    for(const auto &v:list()) {
        auto o=v.toObject();
        if(o["enabled"].toBool()) text+="\nSkill: "+o["name"].toString()+"\n"+o["prompt"].toString()+"\n";
        if(text.size()>kMaxPrompt) break;
    }
    return text.left(kMaxPrompt);
}
namespace {
// The answer in the language of the question: Latin letters get the English one when there is one.
QString answerFor(const QJsonObject &example, const QString &question) {
    int cyrillic=0, latin=0;
    for(const QChar c:question) { if(c.script()==QChar::Script_Cyrillic) ++cyrillic; else if(c.isLetter() && c.unicode()<128) ++latin; }
    const QString en=example["answer_en"].toString();
    return latin>cyrillic && !en.isEmpty() ? en : example["answer"].toString();
}
QStringList wordings(const QJsonObject &example) {
    QStringList all{example["question"].toString()};
    if(example.contains("question_en")) all.append(example["question_en"].toString());
    for(const auto &alias:example["aliases"].toArray()) all.append(alias.toString());
    return all;
}
}
QString SkillStore::recall(const QString &question) const {
    const QString normalized=key(question);
    for(const auto &v:list()) {
        auto o=v.toObject(); if(!o["enabled"].toBool()) continue;
        for(const auto &e:o["examples"].toArray()) {
            const auto example=e.toObject();
            for(const QString &w:wordings(example)) if(key(w)==normalized) return answerFor(example,question);
        }
    }
    return {};
}
QJsonObject SkillStore::similar(const QString &question, double threshold) const {
    const QSet<QString> wanted=text::stems(question);
    if(wanted.isEmpty()) return {};
    QJsonObject best; double bestScore=threshold;
    for(const auto &v:list()) {
        auto o=v.toObject(); if(!o["enabled"].toBool()) continue;
        for(const auto &e:o["examples"].toArray()) {
            const auto example=e.toObject();
            for(const QString &w:wordings(example)) {
                const double score=text::overlap(wanted,text::stems(w));
                if(score>=bestScore) {
                    const bool en=answerFor(example,question)!=example["answer"].toString();
                    bestScore=score;
                    best=QJsonObject{{"question",en?example["question_en"]:example["question"]},{"answer",answerFor(example,question)},
                                     {"skill",en&&o.contains("name_en")?o["name_en"]:o["name"]}};
                }
            }
        }
    }
    return best;
}
}
