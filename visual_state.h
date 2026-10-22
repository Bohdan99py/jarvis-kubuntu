#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace jarvis {

// The state machine behind the HUD. It turns what Jarvis really does — a
// request, how it is answered (route), the answer streaming in, speech, the
// microphone, background jobs, notifications, failures — into one visual
// state and a short event log. It knows nothing about drawing.
//
// States, highest priority first: error, warning, notification (transient,
// they fade back after a few seconds), speaking, listening, processing, idle.
//
// Demo mode makes up events so every effect can be seen; they are marked
// `demo` in the log and in pulse(), and never counted as real activity.
class VisualState : public QObject
{
    Q_OBJECT
    // Inputs, bound from QML.
    Q_PROPERTY(bool busy READ busy WRITE setBusy NOTIFY inputsChanged)
    Q_PROPERTY(bool speaking READ speaking WRITE setSpeaking NOTIFY inputsChanged)
    Q_PROPERTY(bool listening READ listening WRITE setListening NOTIFY inputsChanged)
    // Background jobs running now (installs, KiCad exports, transcription…).
    Q_PROPERTY(int jobs READ jobs WRITE setJobs NOTIFY inputsChanged)
    Q_PROPERTY(bool demo READ demo WRITE setDemo NOTIFY demoChanged)
    // Development: show this state regardless of events ("" = off).
    Q_PROPERTY(QString forcedState READ forcedState WRITE setForcedState NOTIFY stateChanged)
    // Outputs.
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // The reason of a transient state: an error, a warning, a notification title.
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    // How the current request is answered: llm, recall, calculator, … ("" between requests).
    Q_PROPERTY(QString route READ route NOTIFY activityChanged)
    // Routes taken by the current request, in order.
    Q_PROPERTY(QStringList routes READ routes NOTIFY activityChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY activityChanged)
    // Characters per second of the answer being written (smoothed), and so far.
    Q_PROPERTY(double streamRate READ streamRate NOTIFY activityChanged)
    Q_PROPERTY(int streamChars READ streamChars NOTIFY activityChanged)
    // Milliseconds since the current request was sent; 0 when none.
    Q_PROPERTY(qint64 requestAge READ requestAge NOTIFY activityChanged)
    // How long the last answer took, ms.
    Q_PROPERTY(qint64 lastLatency READ lastLatency NOTIFY activityChanged)
    // Counts since start: requests, replies, llm, recall, memory, errors, notices.
    Q_PROPERTY(QVariantMap counters READ counters NOTIFY activityChanged)
    // Newest first: [{time, kind, detail, demo}], at most kMaxLog.
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)

public:
    static constexpr int kMaxLog = 40;

    explicit VisualState(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void setBusy(bool busy);
    bool speaking() const { return m_speaking; }
    void setSpeaking(bool speaking);
    bool listening() const { return m_listening; }
    void setListening(bool listening);
    int jobs() const { return m_jobs; }
    void setJobs(int jobs);
    bool demo() const { return m_demo; }
    void setDemo(bool demo);
    QString forcedState() const { return m_forced; }
    void setForcedState(const QString &state);

    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString route() const { return m_route; }
    QStringList routes() const { return m_routes; }
    bool streaming() const { return m_streaming; }
    double streamRate() const { return m_streamRate; }
    int streamChars() const { return m_streamChars; }
    qint64 requestAge() const;
    qint64 lastLatency() const { return m_lastLatency; }
    QVariantMap counters() const { return m_counters; }
    QVariantList log() const { return m_log; }

    // A real event (see ChatModel::activity for the kinds), or "tool" (detail:
    // what runs), "warning", "voice" (detail: start/stop of speech or dictation).
    Q_INVOKABLE void record(const QString &kind, const QString &detail = {});
    // How long transient states last, ms (tests shorten them).
    void setDurations(int errorMs, int warningMs, int noticeMs);

signals:
    void inputsChanged();
    void demoChanged();
    void stateChanged();
    void activityChanged();
    void logChanged();
    // Every event, for one-off effects (a memory network, a data burst, a panel).
    void pulse(const QString &kind, const QString &detail, bool demo);

private:
    void handle(const QString &kind, const QString &detail, bool demo);
    void append(const QString &kind, const QString &detail, bool demo);
    void transient(const QString &state, const QString &message, int ms);
    void update();
    void demoTick();
    void count(const QString &key);

    bool m_busy = false, m_speaking = false, m_listening = false, m_demo = false, m_streaming = false;
    int m_jobs = 0, m_streamChars = 0;
    QString m_state = QStringLiteral("idle"), m_message, m_route, m_forced;
    QStringList m_routes;
    QString m_transient; // error, warning or notification while its timer runs
    QTimer m_transientTimer, m_demoTimer;
    double m_streamRate = 0;
    qint64 m_lastStreamAt = -1, m_lastLatency = 0, m_lastMemoryAt = -1;
    QElapsedTimer m_request;
    QElapsedTimer m_clock;
    QVariantMap m_counters;
    QVariantList m_log;
    int m_errorMs = 2200, m_warningMs = 4500, m_noticeMs = 3800;
    int m_demoStep = 0;
};

} // namespace jarvis
