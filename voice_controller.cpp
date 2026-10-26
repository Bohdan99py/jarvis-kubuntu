#include "voice_controller.h"
#include "claude_client.h"
#include "runtime_paths.h"
#include "secret_store.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QHash>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace {
const QStringList kEngines{QStringLiteral("auto"),QStringLiteral("elevenlabs"),QStringLiteral("chatterbox"),QStringLiteral("piper"),QStringLiteral("system")};
const QStringList kElevenModels{QStringLiteral("eleven_multilingual_v2"),QStringLiteral("eleven_flash_v2_5"),QStringLiteral("eleven_turbo_v2_5"),QStringLiteral("eleven_v3")};
const QStringList kSampleSuffixes{QStringLiteral("wav"),QStringLiteral("flac"),QStringLiteral("mp3"),QStringLiteral("ogg")};
// "George", a British voice of the ElevenLabs library that every account has.
const QString kElevenDefaultVoice=QStringLiteral("JBFqnCBsd6RMkjVDRZzb");
constexpr qint64 kMaxSampleBytes=20*1024*1024;
}

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
        if(m_inputLevel!=0){m_inputLevel=0;emit levelChanged();}
        if(code!=0 && !m_receivedError) m_status=tr("The voice module failed. Check Python, the model and the microphone.");
        syncServer(); // a voice was installed: the server may now keep it loaded
        emit changed();
    });
    connect(&m_speech,&QProcess::started,this,&VoiceController::changed);
    connect(&m_speech,&QProcess::readyReadStandardOutput,this,&VoiceController::readSpeechEvents);
    connect(&m_speech,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        readSpeechEvents();
        resetOutputLevel();
        if(code!=0 && status==QProcess::NormalExit)m_status=tr("Could not speak the answer. Check the voice settings and Speech Dispatcher.");
        emit changed();
    });
    connect(&m_speech,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){m_status=m_speech.errorString();emit changed();});
    // The server prints nothing useful after "ready"; it ends when its stdin closes.
    m_server.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(&m_server,&QProcess::readyReadStandardOutput,this,[this]{m_server.readAllStandardOutput();});
    if(m_enabled) QTimer::singleShot(0,this,&VoiceController::syncServer);
}
VoiceController::~VoiceController() {
    if(m_server.state()!=QProcess::NotRunning){m_server.closeWriteChannel();if(!m_server.waitForFinished(1500))m_server.kill();}
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
void VoiceController::setEnabled(bool enabled) {
    m_enabled=enabled;QSettings().setValue("voice/enabled",enabled);
    if(!enabled && speaking())m_speech.terminate();
    syncServer();
    emit changed();
}
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
    // A fresh answer replaces the one still being read instead of being dropped.
    if(!m_enabled || text.trimmed().isEmpty() || m_recording)return;
    say(text,{});
}
void VoiceController::say(const QString &text,const QString &lang) {
    if(speaking()){m_speech.terminate();if(!m_speech.waitForFinished(1000))m_speech.kill();}
    syncServer();
    m_speechBuffer.clear();m_speechError=false;
    QStringList args{jarvisScript("speak.py")};
    if(!lang.isEmpty())args.append(lang);
    m_speech.setProcessEnvironment(speechEnvironment());
    m_speech.start(systemPython(),args);
    m_speech.write(text.left(6000).toUtf8());m_speech.closeWriteChannel();
}
void VoiceController::resetOutputLevel() {
    m_outputLevel=0;m_outputBands={0.0,0.0,0.0};m_outputMeasured=false;
    emit levelChanged();
}
void VoiceController::readSpeechEvents() {
    m_speechBuffer+=m_speech.readAllStandardOutput();
    static const QHash<QString,QString> names{{"elevenlabs","ElevenLabs"},{"chatterbox","Chatterbox"},{"piper","Piper"},{"system","Speech Dispatcher"}};
    while(m_speechBuffer.contains('\n')) {
        const int end=m_speechBuffer.indexOf('\n');const auto line=m_speechBuffer.left(end);m_speechBuffer.remove(0,end+1);
        const auto o=QJsonDocument::fromJson(line).object();
        const QString kind=o["event"].toString();
        if(kind=="level") { // ~30 per second: only the HUD listens
            const QJsonArray v=o["v"].toArray();
            m_outputLevel=std::clamp(v.at(0).toDouble(),0.0,1.0);
            m_outputBands={std::clamp(v.at(1).toDouble(),0.0,1.0),std::clamp(v.at(2).toDouble(),0.0,1.0),std::clamp(v.at(3).toDouble(),0.0,1.0)};
            m_outputMeasured=true;
            emit levelChanged();
            continue;
        }
        if(kind=="error") {m_status=o["text"].toString().left(300);m_speechError=true;emit changed();}
        else if(kind=="engine" && m_speechError && names.contains(o["name"].toString())) {
            // The chosen voice failed: say who took over.
            m_status+=u' '+tr("%1 spoke instead.").arg(names.value(o["name"].toString()));m_speechError=false;emit changed();
        }
    }
}

// ---- Speech server ---------------------------------------------------------------

QString VoiceController::serverPython() const {
    // Only local neural voices are worth keeping in memory.
    if(!m_enabled) return {};
    const QString active=activeEngine();
    if(active=="chatterbox") return root()+"/chatterbox/venv/bin/python";
    if(active=="piper" || (active=="elevenlabs" && naturalReady())) return root()+"/venv/bin/python"; // Piper is the fallback
    return {};
}
void VoiceController::syncServer() {
    const QString python=serverPython();
    if(m_server.state()!=QProcess::NotRunning && python==m_serverPython) return;
    if(m_server.state()!=QProcess::NotRunning) {
        m_server.closeWriteChannel();
        if(!m_server.waitForFinished(1500)){m_server.kill();m_server.waitForFinished(500);}
    }
    m_serverPython=python;
    if(python.isEmpty() || !QFileInfo::exists(python)) return;
    m_server.setProcessEnvironment(speechEnvironment());
    m_server.start(python,{jarvisScript("speak.py"),"--serve"});
}

// ---- Settings files ----------------------------------------------------------------

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
QJsonObject readJson(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && f.size()<1024*1024 ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
}
// Owner-only: speech.json holds the ElevenLabs key.
bool writeJson(const QString &path,const QJsonObject &o) {
    const QString dir=QFileInfo(path).absolutePath();
    QDir().mkpath(dir);
    QFile::setPermissions(dir,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)) return false;
    const QByteArray bytes=QJsonDocument(o).toJson();
    return f.write(bytes)==bytes.size() && f.commit();
}
}
// Read once, then kept: only this window writes them, and QML reads them often.
QJsonObject VoiceController::piperSettings() const {
    if(!m_piperCache) m_piperCache=readJson(root()+"/piper/settings.json");
    return *m_piperCache;
}
void VoiceController::savePiperSettings(const QJsonObject &settings) { m_piperCache=settings;writeJson(root()+"/piper/settings.json",settings);emit changed(); }
QJsonObject VoiceController::speechSettings() const {
    if(!m_speechCache) m_speechCache=readJson(root()+"/speech.json");
    return *m_speechCache;
}
void VoiceController::saveSpeechSettings(const QJsonObject &settings) {
    m_speechCache=settings;
    if(!writeJson(root()+"/speech.json",settings)) m_status=tr("Could not save the voice settings.");
    syncServer();
    emit changed();
}
void VoiceController::updateSection(const QString &section,const QString &key,const QJsonValue &value) {
    auto s=speechSettings();auto part=s.value(section).toObject();
    if(value.isUndefined()) part.remove(key); else part[key]=value;
    s[section]=part;saveSpeechSettings(s);
}

// ---- Engines ---------------------------------------------------------------------

QString VoiceController::engine() const {
    const QString e=speechSettings().value("engine").toString();
    return kEngines.contains(e) ? e : QStringLiteral("auto");
}
void VoiceController::setEngine(const QString &value) {
    if(!kEngines.contains(value) || value==engine()) return;
    auto s=speechSettings();s["engine"]=value;saveSpeechSettings(s);
    const QString active=activeEngine();
    if(value!="auto" && value!="system" && active!=value)
        m_status=value=="elevenlabs" ? tr("Enter an ElevenLabs API key to use its voices.")
                 : value=="chatterbox" ? tr("Install Chatterbox to use it.") : tr("Install the natural voice to use Piper.");
    else m_status=tr("Voice changed.");
    emit changed();
}
QString VoiceController::activeEngine() const {
    const QString chosen=engine();
    QStringList order=chosen=="auto" ? QStringList{"elevenlabs","chatterbox","piper","system"} : QStringList{chosen,"piper","system"};
    for(const QString &e:order) {
        if((e=="elevenlabs" && elevenReady()) || (e=="chatterbox" && chatterboxReady()) || (e=="piper" && naturalReady()) || e=="system")
            return e;
    }
    return QStringLiteral("system");
}

// ---- Piper -------------------------------------------------------------------------

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
    m_status.clear();
    say(samples.value(lang,samples.value("en")),lang);
    emit changed();
}

// ---- ElevenLabs --------------------------------------------------------------------

QString VoiceController::elevenKey() const {
    if(!m_elevenKeyCache) {
        QString key=qEnvironmentVariable("ELEVENLABS_API_KEY").trimmed(),where=QStringLiteral("environment");
        if(key.isEmpty()) {key=jarvis::SecretStore::read(QLatin1String(jarvis::SecretStore::kElevenLabs));where=QStringLiteral("keyring");}
        if(key.isEmpty()) {key=speechSettings().value("elevenlabs").toObject().value("api_key").toString();where=QStringLiteral("file");}
        m_elevenKeyCache=std::make_pair(key,key.isEmpty()?QString():where);
    }
    return m_elevenKeyCache->first;
}
QString VoiceController::elevenKeyStore() const { elevenKey(); return m_elevenKeyCache->second; }
QProcessEnvironment VoiceController::speechEnvironment() const {
    QProcessEnvironment env=QProcessEnvironment::systemEnvironment();
    if(elevenKeyStore()==QLatin1String("keyring")) env.insert(QStringLiteral("ELEVENLABS_API_KEY"),elevenKey());
    return env;
}
bool VoiceController::elevenReady() const { return !elevenKey().isEmpty(); }
QString VoiceController::elevenKeyHint() const {
    const QString key=elevenKey();
    return key.size()>8 ? QChar(0x2026)+key.right(4) : QString();
}
QString VoiceController::elevenVoice() const {
    const QString id=speechSettings().value("elevenlabs").toObject().value("voice_id").toString();
    return id.isEmpty() ? kElevenDefaultVoice : id;
}
QString VoiceController::elevenModel() const {
    const QString m=speechSettings().value("elevenlabs").toObject().value("model").toString();
    return kElevenModels.contains(m) ? m : kElevenModels.first();
}
void VoiceController::setElevenModel(const QString &model) { if(kElevenModels.contains(model) && model!=elevenModel()) updateSection("elevenlabs","model",model); }
double VoiceController::elevenStability() const { return speechSettings().value("elevenlabs").toObject().value("stability").toDouble(0.5); }
void VoiceController::setElevenStability(double value) { updateSection("elevenlabs","stability",std::clamp(value,0.0,1.0)); }
void VoiceController::setElevenKey(const QString &key) {
    const QString value=key.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{16,200}$"));
    if(!value.isEmpty() && !valid.match(value).hasMatch()) {m_status=tr("This does not look like an ElevenLabs API key.");emit changed();return;}
    QString error;
    if(jarvis::SecretStore::available()) {
        const QString account=QLatin1String(jarvis::SecretStore::kElevenLabs);
        if(value.isEmpty() ? !jarvis::SecretStore::remove(account,&error) : !jarvis::SecretStore::write(account,QStringLiteral("Jarvis: ElevenLabs API key"),value,&error)) {
            m_status=tr("The key was not saved: %1").arg(error);emit changed();return;
        }
    }
    m_elevenKeyCache.reset();
    // A key in the password store leaves the file; without a store the file keeps it.
    const bool inFile=!jarvis::SecretStore::available() && !value.isEmpty();
    updateSection("elevenlabs","api_key",inFile ? QJsonValue(value) : QJsonValue(QJsonValue::Undefined));
    // The speech server took the old key with its environment.
    if(m_server.state()!=QProcess::NotRunning) {m_server.closeWriteChannel();if(!m_server.waitForFinished(1500))m_server.kill();m_serverPython.clear();syncServer();}
    if(value.isEmpty()) {m_elevenVoices.clear();m_status=tr("The ElevenLabs key was removed.");emit changed();return;}
    m_status=tr("Key saved. Loading your ElevenLabs voices…");
    loadElevenVoices();
}
void VoiceController::loadElevenVoices() {
    if(m_elevenLoading) return;
    const QString key=elevenKey();
    if(key.isEmpty()) {m_status=tr("Enter an ElevenLabs API key to use its voices.");emit changed();return;}
    if(jarvis::ClaudeClient::cloudBlocked()) {m_status=tr("\"Only local\" mode is on: ElevenLabs is not contacted (Settings → Autonomy).");emit changed();return;}
    QNetworkRequest request(QUrl(QStringLiteral("https://api.elevenlabs.io/v1/voices")));
    request.setRawHeader("xi-api-key",key.toUtf8());
    request.setRawHeader("Accept","application/json");
    request.setTransferTimeout(20000);
    m_elevenLoading=true;emit changed();
    auto *reply=m_network.get(request);
    connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>8*1024*1024)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        reply->deleteLater();m_elevenLoading=false;
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(code==401) m_status=tr("ElevenLabs rejected the API key (401).");
        else if(reply->error()!=QNetworkReply::NoError) m_status=tr("Could not reach ElevenLabs: %1").arg(reply->errorString());
        else {
            m_elevenVoices.clear();
            for(const auto &v:QJsonDocument::fromJson(reply->readAll()).object().value("voices").toArray()) {
                const auto o=v.toObject();const auto labels=o.value("labels").toObject();
                QStringList about;
                for(const char *label:{"gender","accent","age","description","use_case"})
                    if(const QString t=labels.value(QLatin1String(label)).toString();!t.isEmpty()) about.append(t);
                m_elevenVoices.append(QVariantMap{{"id",o.value("voice_id").toString().left(40)},{"name",o.value("name").toString().left(80)},
                                                  {"description",about.join(", ").left(120)}});
                if(m_elevenVoices.size()>=300) break;
            }
            std::sort(m_elevenVoices.begin(),m_elevenVoices.end(),[](const QVariant &a,const QVariant &b){
                return a.toMap().value("name").toString().localeAwareCompare(b.toMap().value("name").toString())<0;});
            m_status=tr("ElevenLabs is ready: %1 voices available.").arg(m_elevenVoices.size());
        }
        emit changed();
    });
}
void VoiceController::chooseElevenVoice(const QString &id,const QString &name) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9]{8,40}$"));
    if(!valid.match(id).hasMatch()) return;
    auto s=speechSettings();auto part=s.value("elevenlabs").toObject();
    part["voice_id"]=id;part["voice_name"]=name.left(80);s["elevenlabs"]=part;
    saveSpeechSettings(s);
    m_status=tr("Voice changed.");emit changed();
}

// ---- Chatterbox --------------------------------------------------------------------

bool VoiceController::chatterboxReady() const { return QFileInfo::exists(root()+"/chatterbox/ready") && QFileInfo::exists(root()+"/chatterbox/venv/bin/python"); }
bool VoiceController::hasNvidia() { return !QStandardPaths::findExecutable("nvidia-smi").isEmpty() || QFileInfo::exists("/proc/driver/nvidia/version"); }
QString VoiceController::chatterboxDevice() const {
    const QString d=speechSettings().value("chatterbox").toObject().value("device").toString();
    return d=="cpu"||d=="cuda" ? d : QStringLiteral("auto");
}
void VoiceController::setChatterboxDevice(const QString &device) {
    if((device=="auto"||device=="cpu"||device=="cuda") && device!=chatterboxDevice()) {
        updateSection("chatterbox","device",device);
        // The server holds the model on the old device.
        if(m_server.state()!=QProcess::NotRunning){m_server.closeWriteChannel();m_server.waitForFinished(1500);syncServer();}
    }
}
QString VoiceController::chatterboxSample() const {
    for(const QString &suffix:kSampleSuffixes)
        if(QFileInfo::exists(root()+"/chatterbox/reference."+suffix))
            return speechSettings().value("chatterbox").toObject().value("sample_name").toString(QStringLiteral("reference.")+suffix);
    return {};
}
double VoiceController::chatterboxExaggeration() const { return speechSettings().value("chatterbox").toObject().value("exaggeration").toDouble(0.5); }
void VoiceController::setChatterboxExaggeration(double value) { updateSection("chatterbox","exaggeration",std::clamp(value,0.25,2.0)); }
void VoiceController::installChatterbox() {
    if(busy())return;
    m_status=tr("Installing Chatterbox: this downloads several gigabytes and takes a while…");
    start(systemPython(),{jarvisScript("voice.py"),"chatterbox","--root",root(),"--lang",m_language,
                          "--chatterbox-device",chatterboxDevice()},3*3600*1000);
}
void VoiceController::setChatterboxSample(const QUrl &file) {
    const QString dir=root()+"/chatterbox";
    QDir().mkpath(dir);
    if(file.isEmpty()) {
        for(const QString &suffix:kSampleSuffixes) QFile::remove(dir+"/reference."+suffix);
        updateSection("chatterbox","sample_name",QJsonValue(QJsonValue::Undefined));
        m_status=tr("Chatterbox speaks with its own voice again.");emit changed();return;
    }
    const QFileInfo info(file.toLocalFile());
    const QString suffix=info.suffix().toLower();
    if(!file.isLocalFile() || !info.isFile() || !kSampleSuffixes.contains(suffix) || info.size()>kMaxSampleBytes) {
        m_status=tr("Choose a WAV, FLAC, MP3 or OGG recording up to 20 MB.");emit changed();return;
    }
    for(const QString &s:kSampleSuffixes) QFile::remove(dir+"/reference."+s);
    if(!QFile::copy(info.absoluteFilePath(),dir+"/reference."+suffix)) {m_status=tr("Could not copy the recording.");emit changed();return;}
    QFile::setPermissions(dir+"/reference."+suffix,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    updateSection("chatterbox","sample_name",info.fileName().left(120));
    m_status=tr("Chatterbox will now speak like this recording.");emit changed();
}

void VoiceController::readEvents() {
    m_buffer+=m_worker.readAllStandardOutput();
    while(m_buffer.contains('\n')) {
        const int end=m_buffer.indexOf('\n');const auto line=m_buffer.left(end);m_buffer.remove(0,end+1);
        const auto o=QJsonDocument::fromJson(line).object();const auto kind=o["event"].toString();const auto text=o["text"].toString();
        if(kind=="level") {m_inputLevel=std::clamp(o["v"].toDouble(),0.0,1.0);emit levelChanged();continue;}
        if(kind=="listening")m_recording=true;
        if(kind=="error")m_receivedError=true;
        if(kind=="text") {m_status=text.isEmpty()?tr("No speech recognized. Try again."):tr("Text added to the input field.");if(!text.isEmpty())emit textReady(text);}
        else if(!text.isEmpty())m_status=text;
        emit changed();
    }
}

// ---- Reset (settings window) -------------------------------------------------------

QStringList VoiceController::resetPreview() const {
    QStringList out;
    if(m_enabled) out<<tr("Speak answers aloud: off");
    if(engine()!=QLatin1String("auto")) out<<tr("Voice engine: best available");
    if(!qFuzzyCompare(rate(),1.0)) out<<tr("Speaking pace: 100 %");
    const auto eleven=speechSettings().value("elevenlabs").toObject();
    if(eleven.contains("model") || eleven.contains("stability") || eleven.contains("voice_id")) out<<tr("ElevenLabs voice, model and stability: defaults");
    const auto chatterbox=speechSettings().value("chatterbox").toObject();
    if(chatterbox.contains("device") || chatterbox.contains("exaggeration")) out<<tr("Chatterbox expressiveness and processor: defaults");
    return out;
}
void VoiceController::resetSettings() {
    if(m_enabled) setEnabled(false);
    auto s=speechSettings();
    s.remove("engine");
    auto eleven=s.value("elevenlabs").toObject();
    for(const char *k:{"model","stability","voice_id","voice_name"}) eleven.remove(QLatin1String(k));
    s["elevenlabs"]=eleven;
    auto chatterbox=s.value("chatterbox").toObject();
    for(const char *k:{"device","exaggeration"}) chatterbox.remove(QLatin1String(k));
    s["chatterbox"]=chatterbox;
    saveSpeechSettings(s);
    auto p=piperSettings();p["rate"]=1.0;savePiperSettings(p);
    m_status=tr("Voice settings reset.");
    emit changed();
}
