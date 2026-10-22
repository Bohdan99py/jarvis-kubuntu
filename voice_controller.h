#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <optional>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
// Jarvis's voice: dictation (Vosk) and speech. Speech has engines — ElevenLabs
// (cloud), Chatterbox and Piper (local neural) and Speech Dispatcher — chosen in
// voice/speech.json; scripts/speak.py speaks with them. While a local neural voice
// is in use a speech server (speak.py --serve) keeps its model loaded, so an
// answer starts sounding at once instead of after loading the model every time.
class VoiceController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool speaking READ speaking NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    // Recognition language, "ru" or "en": selects the Vosk model.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    // "auto", "elevenlabs", "chatterbox", "piper" or "system"; `activeEngine` is the one that speaks now.
    Q_PROPERTY(QString engine READ engine WRITE setEngine NOTIFY changed)
    Q_PROPERTY(QString activeEngine READ activeEngine NOTIFY changed)
    // Natural voice (Piper, local neural speech): installed, the voices to choose
    // from ([{id, lang, name, installed}]), the chosen voice per language and the pace.
    Q_PROPERTY(bool naturalReady READ naturalReady NOTIFY changed)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY changed)
    Q_PROPERTY(QVariantMap chosenVoices READ chosenVoices NOTIFY changed)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY changed)
    // ElevenLabs: the key is never shown, only "…abcd". Voices: [{id, name, description}].
    Q_PROPERTY(bool elevenReady READ elevenReady NOTIFY changed)
    Q_PROPERTY(QString elevenKeyHint READ elevenKeyHint NOTIFY changed)
    Q_PROPERTY(QVariantList elevenVoices READ elevenVoices NOTIFY changed)
    Q_PROPERTY(QString elevenVoice READ elevenVoice NOTIFY changed)
    Q_PROPERTY(QString elevenModel READ elevenModel WRITE setElevenModel NOTIFY changed)
    Q_PROPERTY(double elevenStability READ elevenStability WRITE setElevenStability NOTIFY changed)
    Q_PROPERTY(bool elevenLoading READ elevenLoading NOTIFY changed)
    // Chatterbox: installed, device ("auto", "cpu", "cuda"), a voice sample to imitate, expressiveness.
    Q_PROPERTY(bool chatterboxReady READ chatterboxReady NOTIFY changed)
    Q_PROPERTY(bool hasNvidia READ hasNvidia CONSTANT)
    Q_PROPERTY(QString chatterboxDevice READ chatterboxDevice WRITE setChatterboxDevice NOTIFY changed)
    Q_PROPERTY(QString chatterboxSample READ chatterboxSample NOTIFY changed)
    Q_PROPERTY(double chatterboxExaggeration READ chatterboxExaggeration WRITE setChatterboxExaggeration NOTIFY changed)
public:
    explicit VoiceController(QObject *parent=nullptr);
    ~VoiceController() override;
    bool ready() const;
    bool busy() const { return m_worker.state()!=QProcess::NotRunning; }
    bool recording() const { return m_recording; }
    bool speaking() const { return m_speech.state()!=QProcess::NotRunning; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString status() const { return m_status; }
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString engine() const;
    void setEngine(const QString &engine);
    QString activeEngine() const;
    Q_INVOKABLE void setup();
    Q_INVOKABLE void listen();
    Q_INVOKABLE void stop();
    // Speaks an answer; a new answer interrupts the one still being read.
    Q_INVOKABLE void speak(const QString &text);
    bool naturalReady() const;
    QVariantList voices() const;
    QVariantMap chosenVoices() const;
    double rate() const;
    void setRate(double rate);
    // Pick the voice for "ru", "en" or "fr"; download it with installNatural() if it is not installed.
    Q_INVOKABLE void chooseVoice(const QString &lang, const QString &id);
    // Installs Piper and the chosen voices (~63 MB each).
    Q_INVOKABLE void installNatural();
    // Says a sample sentence in `lang` with the current engine.
    Q_INVOKABLE void preview(const QString &lang);

    bool elevenReady() const;
    QString elevenKeyHint() const;
    QVariantList elevenVoices() const { return m_elevenVoices; }
    QString elevenVoice() const;
    QString elevenModel() const;
    void setElevenModel(const QString &model);
    double elevenStability() const;
    void setElevenStability(double value);
    bool elevenLoading() const { return m_elevenLoading; }
    // Saves the key (owner-only file) and fetches the account's voices; "" removes it.
    Q_INVOKABLE void setElevenKey(const QString &key);
    Q_INVOKABLE void loadElevenVoices();
    Q_INVOKABLE void chooseElevenVoice(const QString &id, const QString &name);

    bool chatterboxReady() const;
    static bool hasNvidia();
    QString chatterboxDevice() const;
    void setChatterboxDevice(const QString &device);
    QString chatterboxSample() const;
    double chatterboxExaggeration() const;
    void setChatterboxExaggeration(double value);
    // Installs Chatterbox with PyTorch into its own environment (1–3 GB plus ~3 GB of weights).
    Q_INVOKABLE void installChatterbox();
    // A clear 5–20 s recording of one voice: Chatterbox speaks like it. Empty url removes it.
    Q_INVOKABLE void setChatterboxSample(const QUrl &file);
signals:
    void changed();
    void languageChanged();
    void textReady(const QString &text);
private:
    void start(const QString &python,const QStringList &args,int timeout);
    void readEvents();
    void readSpeechEvents();
    static QString root();
    QJsonObject piperSettings() const;
    void savePiperSettings(const QJsonObject &settings);
    QJsonObject speechSettings() const;
    void saveSpeechSettings(const QJsonObject &settings);
    void updateSection(const QString &section, const QString &key, const QJsonValue &value);
    void say(const QString &text, const QString &lang);
    // Starts, restarts or stops the speech server for the engine in use.
    void syncServer();
    QString serverPython() const;
    QString modelName() const;
    QProcess m_worker,m_speech,m_server;
    QString m_serverPython;
    QTimer m_timeout;
    QByteArray m_buffer,m_speechBuffer;
    QString m_status;
    QString m_language=QStringLiteral("ru");
    QNetworkAccessManager m_network;
    QVariantList m_elevenVoices;
    bool m_elevenLoading=false,m_speechError=false;
    mutable std::optional<QJsonObject> m_speechCache,m_piperCache;
    bool m_recording=false,m_enabled=false,m_receivedError=false;
};
