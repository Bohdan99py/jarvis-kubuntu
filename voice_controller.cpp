#include "voice_controller.h"
#include "runtime_paths.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QHash>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

QString VoiceController::root() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/jarvis/voice"; }
VoiceController::VoiceController(QObject *parent):QObject(parent) {
    m_enabled=QSettings().value("voice/enabled",false).toBool();
    m_status=ready()?tr("Voice is ready"):tr("To dictate, install the speech model (~45 MB).");
    m_timeout.setSingleShot(true);
    connect(&m_timeout,&QTimer::timeout,this,[this]{m_worker.kill();m_recording=false;m_status=tr("The voice operation timed out.");emit changed();});
    connect(&m_worker,&QProcess::readyReadStandardOutput,this,&VoiceController::readEvents);
    connect(&m_worker,&QProcess::readyReadStandardError,this,[this]{m_worker.readAllStandardError();});
    connect(&m_worker,&QProcess::started,this,&VoiceController::changed);
    connect(&m_worker,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){m_timeout.stop();m_recording=false;m_status=m_worker.errorString();emit changed();});
    connect(&m_worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        readEvents();m_timeout.stop();m_recording=false;
        if(code!=0 && !m_receivedError) m_status=tr("The voice module failed. Check Python, the model and the microphone.");
        emit changed();
    });
    connect(&m_speech,&QProcess::started,this,&VoiceController::changed);
    connect(&m_speech,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){if(code!=0)m_status=tr("Could not speak the answer. Check Speech Dispatcher.");emit changed();});
    connect(&m_speech,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){m_status=m_speech.errorString();emit changed();});
}
VoiceController::~VoiceController() {
    for(auto *process:{&m_worker,&m_speech}) if(process->state()!=QProcess::NotRunning){process->terminate();if(!process->waitForFinished(1500)){process->kill();process->waitForFinished(500);}}
}
QString VoiceController::modelName() const { return m_language=="en" ? QStringLiteral("vosk-model-small-en-us-0.15") : QStringLiteral("vosk-model-small-ru-0.22"); }
bool VoiceController::ready() const { return QFileInfo::exists(root()+"/ready") && QFileInfo::exists(root()+"/venv/bin/python") && QFileInfo::exists(root()+"/"+modelName()+"/am/final.mdl"); }
void VoiceController::setLanguage(const QString &language) {
    const QString value=language=="en" ? QStringLiteral("en") : QStringLiteral("ru");
    if(value==m_language) return;
    m_language=value;
    emit languageChanged();
    // Queued: the language is usually set while QML is still evaluating bindings that read `ready`.
    QTimer::singleShot(0,this,[this]{
        if(!busy()) m_status=ready()?tr("Voice is ready"):tr("To dictate, install the speech model (~45 MB).");
        emit changed();
    });
}
void VoiceController::setEnabled(bool enabled) { m_enabled=enabled;QSettings().setValue("voice/enabled",enabled);if(!enabled && speaking())m_speech.terminate();emit changed(); }
void VoiceController::start(const QString &python,const QStringList &args,int timeout) {
    if(busy())return;
    m_receivedError=false;m_buffer.clear();m_worker.start(python,args);m_timeout.start(timeout);emit changed();
}
void VoiceController::setup() { if(busy())return;m_status=tr("Downloading voice components…");start(systemPython(),{jarvisScript("voice.py"),"setup","--root",root(),"--lang",m_language},600000); }
void VoiceController::listen() {
    if(busy())return;
    if(!ready()){m_status=tr("First press \"Install voice\" in settings.");emit changed();return;}
    if(speaking())m_speech.terminate();
    m_status=tr("Preparing the microphone…");
    start(root()+"/venv/bin/python",{jarvisScript("voice.py"),"listen","--root",root(),"--lang",m_language},45000);
}
void VoiceController::stop() {
    if(m_recording){m_worker.write("\n");m_status=tr("Recognizing…");m_recording=false;}
    if(speaking())m_speech.terminate();
    emit changed();
}
void VoiceController::speak(const QString &text) {
    if(!m_enabled || text.trimmed().isEmpty() || busy() || speaking())return;
    say(text,{});
}
void VoiceController::say(const QString &text,const QString &lang) {
    if(speaking()){m_speech.terminate();m_speech.waitForFinished(1000);}
    QStringList args{jarvisScript("speak.py")};
    if(!lang.isEmpty())args.append(lang);
    m_speech.start(systemPython(),args);
    m_speech.write(text.left(6000).toUtf8());m_speech.closeWriteChannel();
}
namespace {
struct VoiceInfo { const char *id; const char *lang; const char *ru; const char *en; };
// Piper voices worth offering: clear, single-speaker, medium quality (~63 MB each).
const VoiceInfo kVoices[]={
    {"ru_RU-dmitri-medium","ru","Дмитрий — мужской","Dmitri — male"},
    {"ru_RU-denis-medium","ru","Денис — мужской","Denis — male"},
    {"ru_RU-ruslan-medium","ru","Руслан — мужской","Ruslan — male"},
    {"ru_RU-irina-medium","ru","Ирина — женский","Irina — female"},
    {"en_GB-alan-medium","en","Alan — британский мужской","Alan — British male"},
    {"en_GB-alba-medium","en","Alba — британский женский","Alba — British female"},
    {"en_US-ryan-medium","en","Ryan — американский мужской","Ryan — American male"},
    {"en_US-amy-medium","en","Amy — американский женский","Amy — American female"},
    {"en_US-lessac-medium","en","Lessac — американский женский","Lessac — American female"},
    {"fr_FR-tom-medium","fr","Tom — французский мужской","Tom — French male"},
    {"fr_FR-siwis-medium","fr","Siwis — французский женский","Siwis — French female"},
};
const char *defaultVoice(const QString &lang) { return lang=="en" ? "en_GB-alan-medium" : lang=="fr" ? "fr_FR-tom-medium" : "ru_RU-dmitri-medium"; }
}
QJsonObject VoiceController::piperSettings() const {
    QFile f(root()+"/piper/settings.json");
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
}
void VoiceController::savePiperSettings(const QJsonObject &settings) {
    QDir().mkpath(root()+"/piper");
    QSaveFile f(root()+"/piper/settings.json");
    if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(settings).toJson());f.commit();}
    emit changed();
}
bool VoiceController::naturalReady() const { return QFileInfo::exists(root()+"/piper/ready") && QFileInfo::exists(root()+"/venv/bin/python"); }
QVariantList VoiceController::voices() const {
    QVariantList out;
    for(const auto &v:kVoices)
        out.append(QVariantMap{{"id",QString::fromLatin1(v.id)},{"lang",QString::fromLatin1(v.lang)},
                               {"name",QString::fromUtf8(m_language=="en"?v.en:v.ru)},
                               {"installed",QFileInfo::exists(root()+"/piper/"+QString::fromLatin1(v.id)+".onnx")}});
    return out;
}
QVariantMap VoiceController::chosenVoices() const {
    const auto s=piperSettings();
    QVariantMap out;
    for(const QString &lang:{QStringLiteral("ru"),QStringLiteral("en"),QStringLiteral("fr")})
        out[lang]=s.value(lang).toString(QString::fromLatin1(defaultVoice(lang)));
    return out;
}
double VoiceController::rate() const { return piperSettings().value("rate").toDouble(1.0); }
void VoiceController::setRate(double rate) {
    auto s=piperSettings();s["rate"]=std::clamp(rate,0.6,1.6);
    for(const QString &lang:{QStringLiteral("ru"),QStringLiteral("en"),QStringLiteral("fr")}) if(!s.contains(lang)) s[lang]=chosenVoices().value(lang).toString();
    savePiperSettings(s);
}
void VoiceController::chooseVoice(const QString &lang,const QString &id) {
    bool known=false;for(const auto &v:kVoices) known=known||(QLatin1String(v.id)==id&&QLatin1String(v.lang)==lang);
    if(!known)return;
    auto s=piperSettings();
    for(const QString &l:{QStringLiteral("ru"),QStringLiteral("en"),QStringLiteral("fr")}) if(!s.contains(l)) s[l]=chosenVoices().value(l).toString();
    s[lang]=id;
    savePiperSettings(s);
    m_status=QFileInfo::exists(root()+"/piper/"+id+".onnx")?tr("Voice changed."):tr("Press \"Download the chosen voices\" to get this voice.");
    emit changed();
}
void VoiceController::installNatural() {
    if(busy())return;
    const auto chosen=chosenVoices();
    // Saved first, so speak.py knows the voices as soon as they are there.
    auto s=piperSettings();for(auto it=chosen.cbegin();it!=chosen.cend();++it) s[it.key()]=it.value().toString();
    if(!s.contains("rate"))s["rate"]=1.0;
    savePiperSettings(s);
    m_status=tr("Installing the natural voice…");
    start(systemPython(),{jarvisScript("voice.py"),"tts","--root",root(),"--lang",m_language,
                          "--voices",QStringList{chosen.value("ru").toString(),chosen.value("en").toString(),chosen.value("fr").toString()}.join(',')},
          1800000);
}
void VoiceController::preview(const QString &lang) {
    static const QHash<QString,QString> samples={
        {"ru",QString::fromUtf8("Привет! Я Джарвис. Делитель из резисторов десять и четыре и семь килоом даст на выходе почти четыре вольта.")},
        {"en",QStringLiteral("Hello, I'm Jarvis. Shall I calculate the resistor for your LED, or remind you about the meeting?")},
        {"fr",QString::fromUtf8("Bonjour ! Je suis Jarvis. On révise un peu de vocabulaire aujourd'hui ?")}};
    say(samples.value(lang,samples.value("en")),lang);
}
void VoiceController::readEvents() {
    m_buffer+=m_worker.readAllStandardOutput();
    while(m_buffer.contains('\n')) {
        const int end=m_buffer.indexOf('\n');const auto line=m_buffer.left(end);m_buffer.remove(0,end+1);
        const auto o=QJsonDocument::fromJson(line).object();const auto kind=o["event"].toString();const auto text=o["text"].toString();
        if(kind=="listening")m_recording=true;
        if(kind=="error")m_receivedError=true;
        if(kind=="text") {m_status=text.isEmpty()?tr("No speech recognized. Try again."):tr("Text added to the input field.");if(!text.isEmpty())emit textReady(text);}
        else if(!text.isEmpty())m_status=text;
        emit changed();
    }
}
