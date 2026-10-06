#include "skill_store.h"
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
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size()>65536) return {};
    const auto bytes=file.readAll();
    return jarvis::SkillStore::validate(bytes).isEmpty() ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
}
}
namespace jarvis {
SkillStore::SkillStore() { initSkills(); }
QString SkillStore::directory() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/jarvis/skills"; }
QString SkillStore::validate(const QByteArray &bytes) {
    if (bytes.size()>65536) return QStringLiteral("Файл навыка больше 64 КБ.");
    QJsonParseError error; auto doc=QJsonDocument::fromJson(bytes,&error);
    if (error.error!=QJsonParseError::NoError || !doc.isObject()) return QStringLiteral("Навык должен быть JSON-объектом.");
    auto o=doc.object();
    static const QRegularExpression id("^[a-z][a-z0-9-]{0,47}$");
    if (o["schema"].toInt()!=1 || !id.match(o["id"].toString()).hasMatch()) return QStringLiteral("Нужны schema: 1 и id из латинских букв, цифр и дефиса.");
    for (const auto &field : {"name","description","prompt"})
        if (!o[field].isString() || o[field].toString().trimmed().isEmpty() || o[field].toString().size()>(QString(field)=="prompt"?4000:300))
            return QStringLiteral("Недопустимое поле навыка: %1").arg(field);
    if (o.contains("examples") && !o["examples"].isArray()) return QStringLiteral("examples должен быть массивом.");
    if (o["examples"].toArray().size()>50) return QStringLiteral("До 50 примеров на навык.");
    for (const auto &v:o["examples"].toArray()) {
        auto example=v.toObject();
        for (const auto &field:{"question","answer"})
            if (!example[field].isString() || example[field].toString().trimmed().isEmpty() || example[field].toString().size()>4096)
                return QStringLiteral("У примера нужны вопрос и ответ до 4096 символов.");
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
    if(!exists) return QStringLiteral("Навык не найден.");
    QSettings s(statePath(),QSettings::IniFormat);s.setValue(id+"/enabled",enabled);s.sync();
    return s.status()==QSettings::NoError ? QString() : QStringLiteral("Не удалось сохранить состояние навыка.");
}
QString SkillStore::install(const QByteArray &bytes) {
    const auto error=validate(bytes);if(!error.isEmpty()) return error;
    const auto o=QJsonDocument::fromJson(bytes).object();const QString id=o["id"].toString();
    const auto current=list();
    if(current.size()>=50) return QStringLiteral("Лимит: 50 навыков.");
    for(const auto &v:current) if(v.toObject()["id"].toString()==id) return QStringLiteral("Навык с таким id уже установлен.");
    if(!QDir().mkpath(directory())) return QStringLiteral("Не удалось создать папку навыков.");
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
        if(text.size()>16000) break;
    }
    return text.left(16000);
}
QString SkillStore::recall(const QString &question) const {
    const QString normalized=key(question);
    for(const auto &v:list()) {
        auto o=v.toObject(); if(!o["enabled"].toBool()) continue;
        for(const auto &e:o["examples"].toArray()) {
            auto example=e.toObject(); if(key(example["question"].toString())==normalized) return example["answer"].toString();
        }
    }
    return {};
}
}
