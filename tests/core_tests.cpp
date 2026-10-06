#include <QCoreApplication>
#include <QDateTime>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include "activity_store.h"
#include "knowledge_store.h"
#include "language.h"
#include "memory_text.h"
#include "skill_store.h"
#include "learning_store.h"
static void require(bool ok,const char *message) { if(!ok)qFatal("%s",message); }
static QString factValue(const QJsonArray &facts,const QString &slot) {
    for(const auto &v:facts) if(v.toObject()["slot"].toString()==slot) return v.toObject()["value"].toString();
    return {};
}
static void skills() {
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
}
static void examples() {
    jarvis::LearningStore memory;
    require(memory.teach("A Question?","Correct answer").isEmpty(),"save memory");
    require(memory.recall("a question")=="Correct answer","recall memory");
    require(memory.teach("A Question","Updated").isEmpty(),"replace memory");
    require(memory.items().size()==1,"replace not append");
    require(!memory.teach("","bad").isEmpty(),"empty memory rejected");
    require(memory.teach(QStringLiteral("Какой редактор я использую для кода?"),QStringLiteral("Kate")).isEmpty(),"save second example");
    const auto similar=memory.similar(QStringLiteral("какой у меня редактор кода"),0.3,3);
    require(!similar.isEmpty() && similar.first().answer=="Kate","similar question found by stems");
    require(memory.similar(QStringLiteral("погода в Берлине"),0.3,3).isEmpty(),"unrelated question not similar");
}
static void language() {
    jarvis::Lang lang;
    require(jarvis::detectLanguage(QStringLiteral("Привет"),&lang) && lang==jarvis::Lang::Ru,"detect Russian");
    require(jarvis::detectLanguage(QStringLiteral("What time is it?"),&lang) && lang==jarvis::Lang::En,"detect English");
    require(!jarvis::detectLanguage(QStringLiteral("42 :)"),&lang),"digits are undecided");
    require(jarvis::resolveLanguage("en")==jarvis::Lang::En && jarvis::resolveLanguage("ru")==jarvis::Lang::Ru,"explicit language");
    require(jarvis::text::looksSensitive(QStringLiteral("мой пароль hunter2")),"password is sensitive");
    require(jarvis::text::looksSensitive(QStringLiteral("card 4111 1111 1111 1111")),"card number is sensitive");
    require(!jarvis::text::looksSensitive(QStringLiteral("I use Kate")),"ordinary text is fine");
}
static void knowledge() {
    jarvis::KnowledgeStore k;
    k.observe(QStringLiteral("Привет! Меня зовут богдан, я работаю программистом."),true);
    k.observe(QStringLiteral("I use Kate for coding, but sometimes vim."),true);
    k.observe(QStringLiteral("я работаю дома"),true);
    k.observe(QStringLiteral("Как меня зовут?"),true);
    k.observe(QStringLiteral("I use it to build things"),true);
    k.observe(QStringLiteral("Мой пароль от почты qwerty"),true);
    auto facts=k.facts();
    require(factValue(facts,"name")==QStringLiteral("Богдан"),"name learned and capitalised");
    require(factValue(facts,"occupation")==QStringLiteral("программистом"),"occupation learned, 'работаю дома' ignored");
    require(factValue(facts,"uses")=="Kate","tool learned and trimmed");
    require(facts.size()==3,"questions, pronouns and secrets teach nothing");
    k.observe(QStringLiteral("My name is Alex"),true);
    facts=k.facts();
    require(factValue(facts,"name")=="Alex" && facts.size()==3,"single-valued slot replaced");
    require(k.remember(QStringLiteral("предпочитает тёмные темы"),"manual").isEmpty(),"manual note");
    require(k.remember(QStringLiteral("ключ sk-ant-123456"),"manual")=="sensitive","sensitive note refused");
    require(k.describe(jarvis::Lang::Ru).contains(QStringLiteral("тёмные темы")),"describe lists notes");
    require(k.profileForPrompt().contains("name: Alex"),"profile for Claude");
    require(k.topics().contains(QStringLiteral("coding")),"topics counted");
    const auto graph=QJsonDocument::fromJson(k.graph({}).toUtf8()).object();
    require(graph["facts"].toInt()==4 && graph["nodes"].toArray().size()>0,"graph from facts and topics");
    require(k.forget(QStringLiteral("it"))==0,"short queries forget nothing");
    require(k.forget(QStringLiteral("тёмные темы"))==1,"forget by text");
    require(k.facts().size()==3,"note removed");
    k.clear();
    require(k.facts().isEmpty(),"clear");
}
static void activity() {
    const qint64 t0=QDateTime(QDate::currentDate(),QTime(10,0)).toMSecsSinceEpoch();
    {
        jarvis::ActivityStore a;
        a.setKeepTitles(true);
        require(jarvis::ActivityStore::category("code","code")=="coding","vscode is coding");
        require(jarvis::ActivityStore::category("org.kde.konsole","org.kde.konsole")=="terminal","konsole is terminal");
        require(jarvis::ActivityStore::category("firefox","org.mozilla.firefox")=="browsing","firefox is browsing");
        require(jarvis::ActivityStore::privateTitle(QStringLiteral("Mozilla Firefox Private Browsing")),"private title");
        a.windowActivated("main.cpp — jarvis","code","code",t0);
        a.tick(t0+10*60*1000);
        a.windowActivated("Private Browsing","firefox","firefox",t0+10*60*1000);
        // 60 minutes without a single event: only the idle cap (15 min) counts.
        a.tick(t0+70*60*1000);
        a.windowActivated("","plasmashell","org.kde.plasmashell",t0+70*60*1000);
        require(!a.hasCurrent(),"shell surfaces are not activity");
        a.recordAction("launch:terminal","Terminal",t0);
        a.recordAction("launch:terminal","Terminal",t0+1000);
        const auto snap=a.snapshot(t0+71*60*1000);
        const auto apps=snap["today"].toObject()["apps"].toArray();
        require(apps.size()==2,"two apps today");
        require(qAbs(apps[1].toObject()["seconds"].toDouble()-600)<1,"ten minutes in the editor");
        require(qAbs(apps[0].toObject()["seconds"].toDouble()-900)<1,"idle cap applied");
        for(const auto &v:snap["recent"].toArray()) require(!v.toObject()["title"].toString().contains("Private"),"private titles dropped");
        require(snap["transitions"].toArray().size()==1,"switch recorded");
        bool suggested=false;
        for(const auto &v:a.suggestions(t0)) suggested=suggested||v.toObject()["id"].toString()=="launch:terminal";
        require(suggested,"repeated action suggested");
        require(a.todayText(jarvis::Lang::En,t0+71*60*1000,true).contains("coding"),"today summary");
        require(a.flush(),"activity saved");
    }
    jarvis::ActivityStore reopened;
    require(reopened.snapshot(t0)["today"].toObject()["apps"].toArray().size()==2,"activity persists");
    reopened.clear();
    require(reopened.snapshot(t0)["today"].toObject()["apps"].toArray().isEmpty(),"activity cleared");
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir scratch;require(scratch.isValid(),"temporary directory");
    qputenv("XDG_CONFIG_HOME",(scratch.path()+"/config").toUtf8());
    qputenv("XDG_DATA_HOME",(scratch.path()+"/data").toUtf8());
    skills();
    examples();
    language();
    knowledge();
    activity();
    qInfo()<<"PASS: skills, examples, language, knowledge, activity";
}
