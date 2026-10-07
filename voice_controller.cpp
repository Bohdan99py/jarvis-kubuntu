#include "voice_controller.h"
#include "runtime_paths.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

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
    m_speech.start(systemPython(),{jarvisScript("speak.py")});
    m_speech.write(text.left(6000).toUtf8());m_speech.closeWriteChannel();
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
