#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <functional>

class QNetworkReply;

namespace jarvis {

// The local language model (0.22): llama.cpp's llama-server on this computer,
// started on demand on 127.0.0.1 and stopped again after some idle minutes,
// so nothing runs in the background without a reason. Jarvis never downloads
// weights by itself: the user starts that in Settings → Autonomy (or with
// scripts/models.py), which writes models/verified.json after the sha256 check.
//
// One request at a time; each has a time limit. A crashed server is reported
// honestly and restarted at most once for the next request.
class LocalModel : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LocalModel)

public:
    struct Entry
    {
        QString id, name, file, url, sha256, license, source, noteRu, noteEn;
        qint64 size = 0;
        int vramMb = 0, ramMb = 0;
        bool recommended = false, measured = false;
    };
    enum class Server { Stopped, Starting, Ready, Failed };

    explicit LocalModel(QObject *parent = nullptr);
    ~LocalModel() override;

    static QList<Entry> catalog();
    static Entry entry(const QString &id);
    // ~/.local/share/jarvis/models
    static QString modelsDir();
    // missing, partial, unverified, ready, corrupt.
    static QString fileState(const Entry &e);
    // The llama-server program: $JARVIS_LLAMA_SERVER, then PATH; "" if none.
    static QString serverProgram();

    // Which model to use ("" = none), where (auto, gpu, cpu) and for how long to keep it loaded.
    void configure(const QString &modelId, const QString &device, int keepMinutes);
    // The chosen model is installed and verified and llama-server exists.
    bool installed() const;
    // Why it cannot be used, for people ("" when it can).
    QString unavailableReason(bool ru) const;
    Server server() const { return m_server; }
    QJsonObject status() const;

    // A chat completion constrained by `schema` (JSON schema); `done(ok, json, error)`.
    // Starts the server first when needed. `timeoutMs` covers the answer, not the start.
    using Done = std::function<void(bool ok, const QJsonObject &answer, const QString &error)>;
    void complete(const QString &system, const QString &user, const QJsonObject &schema, int maxTokens, int timeoutMs, Done done);
    // What the first request will look like: the server is warmed up with it while it
    // starts, so the GPU kernels are compiled and the long prompt is cached before
    // the user waits for an answer.
    void setWarmUp(const QString &system, const QJsonObject &schema)
    {
        m_warmSystem = system;
        m_warmSchema = schema;
    }
    // Abandons the request in flight (its callback gets an error) — the server keeps running.
    void abort();
    void stop();
    bool busy() const { return bool(m_done); }

    // Measurements of the last start and request, for diagnostics.
    qint64 lastStartMs() const { return m_lastStartMs; }
    qint64 lastRequestMs() const { return m_lastRequestMs; }

signals:
    void changed();

private:
    void start();
    void probeDevices();
    void launch(const QString &device);
    void pollHealth();
    void warmUp();
    void send();
    void finishRequest(bool ok, const QJsonObject &answer, const QString &error);
    void serverDied(int code, QProcess::ExitStatus status);

    QNetworkAccessManager m_net;
    QProcess m_process;
    QProcess m_probe;
    QTimer m_idle;
    QTimer m_health;
    QTimer m_timeout;
    QElapsedTimer m_startClock;
    QElapsedTimer m_requestClock;
    QPointer<QNetworkReply> m_reply;
    QString m_modelId;
    QString m_device = QStringLiteral("auto");
    QString m_chosenDevice;
    QString m_lastError;
    int m_keepMinutes = 10;
    int m_port = 0;
    int m_crashes = 0;
    qint64 m_lastCrash = 0;
    qint64 m_lastStartMs = -1;
    qint64 m_lastRequestMs = -1;
    Server m_server = Server::Stopped;
    bool m_stopping = false;
    // The request waiting for the server or in flight.
    QJsonObject m_body;
    QString m_warmSystem;
    QJsonObject m_warmSchema;
    int m_timeoutMs = 30000;
    Done m_done;
};

} // namespace jarvis
