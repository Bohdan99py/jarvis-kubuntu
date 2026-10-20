#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
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
    // Natural voice (Piper, local neural speech): installed, the voices to choose
    // from ([{id, lang, name, installed}]), the chosen voice per language and the pace.
    Q_PROPERTY(bool naturalReady READ naturalReady NOTIFY changed)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY changed)
    Q_PROPERTY(QVariantMap chosenVoices READ chosenVoices NOTIFY changed)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY changed)
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
    Q_INVOKABLE void setup();
    Q_INVOKABLE void listen();
    Q_INVOKABLE void stop();
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
    // Says a sample sentence in `lang` with the chosen voice.
    Q_INVOKABLE void preview(const QString &lang);
signals:
    void changed();
    void languageChanged();
    void textReady(const QString &text);
private:
    void start(const QString &python,const QStringList &args,int timeout);
    void readEvents();
    static QString root();
    QJsonObject piperSettings() const;
    void savePiperSettings(const QJsonObject &settings);
    void say(const QString &text, const QString &lang);
    QString modelName() const;
    QProcess m_worker,m_speech;
    QTimer m_timeout;
    QByteArray m_buffer;
    QString m_status;
    QString m_language=QStringLiteral("ru");
    bool m_recording=false,m_enabled=false,m_receivedError=false;
};
