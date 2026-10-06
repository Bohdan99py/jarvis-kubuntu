#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
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
signals:
    void changed();
    void languageChanged();
    void textReady(const QString &text);
private:
    void start(const QString &python,const QStringList &args,int timeout);
    void readEvents();
    static QString root();
    QString modelName() const;
    QProcess m_worker,m_speech;
    QTimer m_timeout;
    QByteArray m_buffer;
    QString m_status;
    QString m_language=QStringLiteral("ru");
    bool m_recording=false,m_enabled=false,m_receivedError=false;
};
