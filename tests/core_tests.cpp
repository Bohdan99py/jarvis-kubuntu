#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include "skill_store.h"
#include "learning_store.h"
static void require(bool ok,const char *message) { if(!ok)qFatal("%s",message); }
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir scratch;require(scratch.isValid(),"temporary directory");
    qputenv("XDG_CONFIG_HOME",(scratch.path()+"/config").toUtf8());
    qputenv("XDG_DATA_HOME",(scratch.path()+"/data").toUtf8());
    jarvis::SkillStore skills;
    require(skills.list().size()==5,"five bundled skills");
    require(!skills.recall(QStringLiteral("Что такое закон Ома?")).isEmpty(),"built-in local example");
    require(skills.setEnabled("electronics",false).isEmpty(),"disable skill");
    require(skills.recall(QStringLiteral("Что такое закон Ома?")).isEmpty(),"disabled skill cannot answer");
    require(!skills.prompt().contains("component ratings"),"disabled skill not sent to Claude");
    jarvis::SkillStore reopened;
    require(reopened.recall(QStringLiteral("Что такое закон Ома?")).isEmpty(),"disabled state persists");
    QJsonObject skill{{"schema",1},{"id","custom"},{"name","Custom"},{"description","Test"},{"prompt","Custom guidance"},
        {"examples",QJsonArray{QJsonObject{{"question","Example question"},{"answer","Example answer"}}}}};
    require(skills.install(QJsonDocument(skill).toJson()).isEmpty(),"import skill");
    require(skills.recall("Example question").isEmpty(),"import starts disabled");
    require(skills.setEnabled("custom",true).isEmpty(),"enable imported skill");
    require(skills.recall("EXAMPLE question!")=="Example answer","normalization");
    require(!skills.install(QJsonDocument(skill).toJson()).isEmpty(),"duplicate rejected");
    skill["id"]="../escape";require(!skills.validate(QJsonDocument(skill).toJson()).isEmpty(),"path traversal rejected");
    skill["id"]="test";skill["prompt"]=QString(4001,'x');require(!skills.validate(QJsonDocument(skill).toJson()).isEmpty(),"oversize prompt rejected");
    require(!skills.validate("[]").isEmpty(),"invalid schema rejected");
    jarvis::LearningStore memory;
    require(memory.teach("A Question?","Correct answer").isEmpty(),"save memory");
    require(memory.recall("a question")=="Correct answer","recall memory");
    require(memory.teach("A Question","Updated").isEmpty(),"replace memory");
    require(QJsonDocument::fromJson(memory.graph().toUtf8()).object()["examples"].toInt()==1,"replace not append");
    require(!memory.teach("","bad").isEmpty(),"empty memory rejected");
    qInfo()<<"PASS: skills, persistence, imports, disable semantics, memory";
}
