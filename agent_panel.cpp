#include "agent_panel.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "agent_system.h"
#include "dbus_names.h"
#include "language.h"
#include "local_model.h"
#include "runtime_paths.h"

using namespace Qt::StringLiterals;

AgentPanel::AgentPanel(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AgentPanel::readEvents);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readEvents();
        if ((code != 0 || status != QProcess::NormalExit) && m_jobError.isEmpty())
            m_jobError = status == QProcess::NormalExit ? tr("Stopped.") : tr("The download was interrupted.");
        m_job.clear();
        emit jobChanged();
        emit changed();
        refresh();
    });
    // The model's state in jarvisd (loaded, loading) changes on its own.
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &AgentPanel::refresh);
    m_poll.start();
    refresh();
}

AgentPanel::~AgentPanel()
{
    // A download goes on resuming next time; it is not left running without the window.
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        m_process.waitForFinished(2000);
    }
}

QVariantList AgentPanel::models() const
{
    QVariantList out;
    for (const jarvis::LocalModel::Entry &e : jarvis::LocalModel::catalog())
        out.append(QVariantMap{{u"id"_s, e.id},
                               {u"name"_s, e.name},
                               {u"sizeGb"_s, e.size / 1e9},
                               {u"license"_s, e.license},
                               {u"source"_s, e.source},
                               {u"state"_s, jarvis::LocalModel::fileState(e)},
                               {u"recommended"_s, e.recommended},
                               {u"measured"_s, e.measured},
                               {u"vramMb"_s, e.vramMb},
                               {u"ramMb"_s, e.ramMb},
                               {u"noteRu"_s, e.noteRu},
                               {u"noteEn"_s, e.noteEn}});
    return out;
}

QString AgentPanel::llamaServer() const { return jarvis::LocalModel::serverProgram(); }

QVariantMap AgentPanel::voice() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/voice"_s;
    QStringList dictation;
    for (const auto &[lang, dir] : {std::pair{u"ru"_s, u"vosk-model-small-ru-0.22"_s}, std::pair{u"en"_s, u"vosk-model-small-en-us-0.15"_s}})
        if (QFileInfo(root + u'/' + dir).isDir())
            dictation.append(lang);
    int voices = 0;
    for (const QFileInfo &f : QDir(root + u"/piper"_s).entryInfoList({u"*.onnx"_s}, QDir::Files))
        voices += f.size() > 0 ? 1 : 0;
    return {{u"dictation"_s, dictation},
            {u"dictationEngine"_s, QFileInfo::exists(root + u"/ready"_s) && QFileInfo::exists(root + u"/venv/bin/python"_s)},
            {u"natural"_s, QFileInfo::exists(root + u"/piper/ready"_s) && voices > 0},
            {u"voices"_s, voices},
            {u"system"_s, !QStandardPaths::findExecutable(u"spd-say"_s).isEmpty()}};
}

void AgentPanel::runScript(const QStringList &args, const QString &id, const QString &phase)
{
    if (m_process.state() != QProcess::NotRunning)
        return;
    m_jobError.clear();
    m_buffer.clear();
    m_job = {{u"id"_s, id}, {u"phase"_s, phase}, {u"done"_s, 0.0}, {u"total"_s, 0.0}};
    emit jobChanged();
    m_process.start(systemPython(), QStringList{jarvisScript(u"models.py"_s)} + args);
}

void AgentPanel::download(const QString &id) { runScript({u"download"_s, id, u"--yes"_s}, id, u"download"_s); }
void AgentPanel::verify(const QString &id) { runScript({u"verify"_s, id}, id, u"verify"_s); }

void AgentPanel::cancel()
{
    if (m_process.state() != QProcess::NotRunning)
        m_process.terminate();
}

QString AgentPanel::remove(const QString &id)
{
    if (m_process.state() != QProcess::NotRunning)
        return tr("Wait for the download or the check to finish.");
    QProcess p;
    p.start(systemPython(), {jarvisScript(u"models.py"_s), u"remove"_s, id});
    if (!p.waitForFinished(10000) || p.exitCode() != 0)
        return tr("Could not remove the model.");
    emit changed();
    return {};
}

void AgentPanel::readEvents()
{
    m_buffer += m_process.readAllStandardOutput();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QJsonObject e = QJsonDocument::fromJson(m_buffer.left(nl)).object();
        m_buffer.remove(0, nl + 1);
        const QString event = e.value(u"event"_s).toString();
        if (event == u"progress" || event == u"verifying" || event == u"start") {
            m_job[u"phase"_s] = event == u"verifying" ? u"verify"_s : u"download"_s;
            m_job[u"done"_s] = e.value(u"done"_s).toDouble(e.value(u"resumed"_s).toDouble());
            m_job[u"total"_s] = e.value(u"total"_s).toDouble();
            emit jobChanged();
        } else if (event == u"error") {
            m_jobError = e.value(u"text"_s).toString().left(300);
            emit jobChanged();
        } else if (event == u"ready") {
            emit changed();
        }
    }
}

void AgentPanel::refresh()
{
    emit changed();
    QDBusMessage m = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), u"/org/jarvis/Integration1"_s,
                                                    u"org.jarvis.Integration1"_s, u"AgentStatus"_s);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusPendingReply<QString> r = *w;
        const QVariantMap now = r.isError() ? QVariantMap{} : QJsonDocument::fromJson(r.value().toUtf8()).object().toVariantMap();
        if (now != m_daemon) {
            m_daemon = now;
            emit daemonChanged();
        }
    });
}

void AgentPanel::runDiagnostics()
{
    QDBusMessage m = QDBusMessage::createMethodCall(QString(jarvis::dbus::kService), u"/org/jarvis/Integration1"_s,
                                                    u"org.jarvis.Integration1"_s, u"AgentDiagnostics"_s);
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m, 15000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusPendingReply<QString> r = *w;
        QJsonObject caps;
        if (!r.isError()) {
            caps = QJsonDocument::fromJson(r.value().toUtf8()).object();
        } else {
            // Without jarvisd: what the window itself can see.
            jarvis::agent::LinuxSystem system;
            for (const auto &[name, error] : system.capabilities())
                caps[name] = QJsonObject{{u"ok"_s, error.isEmpty()}, {u"detail"_s, error}};
            caps[u"service"_s] = QJsonObject{{u"ok"_s, false}, {u"detail"_s, tr("the Jarvis service (jarvisd) is not running")}};
        }
        m_diagnostics.clear();
        for (auto it = caps.begin(); it != caps.end(); ++it) {
            const QJsonObject c = it.value().toObject();
            m_diagnostics.append(QVariantMap{{u"name"_s, it.key()}, {u"ok"_s, c.value(u"ok"_s).toBool()}, {u"detail"_s, c.value(u"detail"_s).toString()}});
        }
        emit daemonChanged();
    });
}
