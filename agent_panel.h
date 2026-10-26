#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Settings → Autonomy (0.22), the parts that are not config.json values
// (those go through AppSettings::setAgent): the local models — what is
// installed and verified, downloading one (only when the user presses the
// button, after seeing its size), verifying and removing it — llama.cpp, the
// voice components on this computer, the agent's state in jarvisd and the
// diagnostics of what works here.
class AgentPanel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // [{id, name, sizeGb, license, source, state, recommended, measured, note}]
    Q_PROPERTY(QVariantList models READ models NOTIFY changed FINAL)
    // The llama-server program, "" when llama.cpp is not installed.
    Q_PROPERTY(QString llamaServer READ llamaServer NOTIFY changed FINAL)
    // A download or a check in progress: {id, phase: download|verify, done, total}; empty when none.
    Q_PROPERTY(QVariantMap job READ job NOTIFY jobChanged FINAL)
    Q_PROPERTY(QString jobError READ jobError NOTIFY jobChanged FINAL)
    // From jarvisd: {mode, model: {server, device, startMs, requestMs, error}, task: {…}}; empty without it.
    Q_PROPERTY(QVariantMap daemon READ daemon NOTIFY daemonChanged FINAL)
    // [{name, ok, detail}] after runDiagnostics().
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY daemonChanged FINAL)
    // Local voice: {dictation: [ru, en installed], natural: Piper installed, voices: n}.
    Q_PROPERTY(QVariantMap voice READ voice NOTIFY changed FINAL)

public:
    explicit AgentPanel(QObject *parent = nullptr);
    ~AgentPanel() override;

    QVariantList models() const;
    QString llamaServer() const;
    QVariantMap job() const { return m_job; }
    QString jobError() const { return m_jobError; }
    QVariantMap daemon() const { return m_daemon; }
    QVariantList diagnostics() const { return m_diagnostics; }
    QVariantMap voice() const;

    // Downloads the model (once; resumes a partial file) and verifies it. Only on request.
    Q_INVOKABLE void download(const QString &id);
    Q_INVOKABLE void verify(const QString &id);
    Q_INVOKABLE void cancel();
    // "" or why not.
    Q_INVOKABLE QString remove(const QString &id);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void runDiagnostics();

signals:
    void changed();
    void jobChanged();
    void daemonChanged();

private:
    void runScript(const QStringList &args, const QString &id, const QString &phase);
    void readEvents();

    QProcess m_process;
    QByteArray m_buffer;
    QVariantMap m_job;
    QString m_jobError;
    QVariantMap m_daemon;
    QVariantList m_diagnostics;
    QTimer m_poll;
};
