#include "local_model.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QThread>

#include "gpu_devices.h"
#include "runtime_paths.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

// The first answer of a fresh server compiles the GPU's shaders: allow for it once.
constexpr int kStartTimeoutMs = 180000;
constexpr int kContext = 4096;

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.read(1 << 20)).object() : QJsonObject{};
}

} // namespace

LocalModel::LocalModel(QObject *parent)
    : QObject(parent)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (!m_done) {
            qInfo() << "local model idle: stopping llama-server";
            stop();
        }
    });
    m_health.setInterval(500);
    connect(&m_health, &QTimer::timeout, this, &LocalModel::pollHealth);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply)
            m_reply->abort();
        if (m_server == Server::Starting) {
            m_lastError = u"the model did not load in time"_s;
            stop();
            m_server = Server::Failed;
        }
        finishRequest(false, {}, u"timeout"_s);
    });
    connect(&m_process, &QProcess::finished, this, &LocalModel::serverDied);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_server = Server::Failed;
            m_lastError = u"llama-server could not start: "_s + m_process.errorString();
            m_health.stop();
            finishRequest(false, {}, m_lastError);
            emit changed();
        }
    });
    // Never mixed into Jarvis's own log: llama-server is chatty.
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, [this] {
        const QByteArray out = m_process.readAll();
        if (out.contains("error") || out.contains("failed"))
            for (const QByteArray &line : out.split('\n'))
                if (line.contains("error") || line.contains("failed"))
                    m_lastError = QString::fromUtf8(line).trimmed().left(300);
    });
}

LocalModel::~LocalModel()
{
    m_done = nullptr;
    stop();
}

QList<LocalModel::Entry> LocalModel::catalog()
{
    QList<Entry> out;
    QFile f(u":/jarvis/models.json"_s);
    if (!f.open(QIODevice::ReadOnly))
        f.setFileName(jarvisDataFile(u"models.json"_s));
    if (!f.isOpen() && !f.open(QIODevice::ReadOnly))
        return out;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(u"models"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(u"id"_s).toString();
        e.name = o.value(u"name"_s).toString();
        e.file = QFileInfo(o.value(u"file"_s).toString()).fileName(); // never a path
        e.url = o.value(u"url"_s).toString();
        e.sha256 = o.value(u"sha256"_s).toString().toLower();
        e.license = o.value(u"license"_s).toString();
        e.source = o.value(u"source"_s).toString();
        e.noteRu = o.value(u"note_ru"_s).toString();
        e.noteEn = o.value(u"note_en"_s).toString();
        e.size = qint64(o.value(u"size"_s).toDouble());
        e.vramMb = o.value(u"vram_mb"_s).toInt();
        e.ramMb = o.value(u"ram_mb"_s).toInt();
        e.recommended = o.value(u"recommended"_s).toBool();
        e.measured = o.value(u"measured"_s).toBool();
        if (!e.id.isEmpty() && !e.file.isEmpty())
            out.append(e);
    }
    return out;
}

LocalModel::Entry LocalModel::entry(const QString &id)
{
    for (const Entry &e : catalog())
        if (e.id == id)
            return e;
    return {};
}

QString LocalModel::modelsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/models"_s;
}

QString LocalModel::fileState(const Entry &e)
{
    if (e.file.isEmpty())
        return u"missing"_s;
    const QString path = modelsDir() + u'/' + e.file;
    const QFileInfo fi(path);
    if (!fi.exists())
        return QFileInfo::exists(path + u".part"_s) ? u"partial"_s : u"missing"_s;
    if (e.size > 0 && fi.size() != e.size)
        return u"corrupt"_s;
    // models.py records the checksum it verified together with the size and time it saw.
    const QJsonObject v = readJson(modelsDir() + u"/verified.json"_s).value(e.file).toObject();
    if (v.value(u"sha256"_s).toString() == e.sha256 && qint64(v.value(u"size"_s).toDouble()) == fi.size()
        && qint64(v.value(u"mtime"_s).toDouble()) == fi.lastModified().toSecsSinceEpoch())
        return u"ready"_s;
    return u"unverified"_s;
}

QString LocalModel::serverProgram()
{
    const QString env = qEnvironmentVariable("JARVIS_LLAMA_SERVER");
    if (!env.isEmpty() && QFileInfo(env).isExecutable())
        return env;
    const QString found = QStandardPaths::findExecutable(u"llama-server"_s);
    if (!found.isEmpty())
        return found;
    return QFileInfo(u"/usr/bin/llama-server"_s).isExecutable() ? u"/usr/bin/llama-server"_s : QString();
}

void LocalModel::configure(const QString &modelId, const QString &device, int keepMinutes)
{
    const bool restart = modelId != m_modelId || device != m_device;
    m_modelId = modelId;
    m_device = device.isEmpty() ? u"auto"_s : device;
    m_keepMinutes = std::clamp(keepMinutes, 1, 120);
    if (restart && m_server != Server::Stopped)
        stop();
    emit changed();
}

bool LocalModel::installed() const
{
    return !m_modelId.isEmpty() && fileState(entry(m_modelId)) == u"ready" && !serverProgram().isEmpty();
}

QString LocalModel::unavailableReason(bool ru) const
{
    if (m_modelId.isEmpty())
        return ru ? u"локальная модель не выбрана (Настройки → Автономность)"_s : u"no local model is chosen (Settings → Autonomy)"_s;
    const Entry e = entry(m_modelId);
    const QString state = fileState(e);
    if (state == u"missing" || state == u"partial")
        return ru ? u"модель %1 не скачана (Настройки → Автономность → «Скачать», %2 ГБ, один раз при интернете)"_s.arg(e.name).arg(e.size / 1e9, 0, 'f', 1)
                  : u"the model %1 is not downloaded (Settings → Autonomy → Download, %2 GB, once while online)"_s.arg(e.name).arg(e.size / 1e9, 0, 'f', 1);
    if (state == u"corrupt")
        return ru ? u"файл модели повреждён (размер не совпадает) — скачайте его заново"_s : u"the model file is damaged (wrong size) — download it again"_s;
    if (state == u"unverified")
        return ru ? u"файл модели не проверен — нажмите «Проверить» в Настройках → Автономность"_s
                  : u"the model file is not verified — press Verify in Settings → Autonomy"_s;
    if (serverProgram().isEmpty())
        return ru ? u"не установлен llama.cpp (sudo apt install llama.cpp-tools libggml0-backend-vulkan)"_s
                  : u"llama.cpp is not installed (sudo apt install llama.cpp-tools libggml0-backend-vulkan)"_s;
    if (m_server == Server::Failed)
        return (ru ? u"локальная модель не запустилась: "_s : u"the local model failed to start: "_s) + m_lastError;
    return {};
}

QJsonObject LocalModel::status() const
{
    static const char *names[] = {"stopped", "starting", "ready", "failed"};
    const Entry e = entry(m_modelId);
    return {{u"model"_s, m_modelId},
            {u"name"_s, e.name},
            {u"file"_s, fileState(e)},
            {u"program"_s, serverProgram()},
            {u"server"_s, QString::fromLatin1(names[int(m_server)])},
            {u"device"_s, m_chosenDevice},
            {u"error"_s, m_lastError},
            {u"startMs"_s, double(m_lastStartMs)},
            {u"requestMs"_s, double(m_lastRequestMs)},
            {u"keepMinutes"_s, m_keepMinutes}};
}

void LocalModel::complete(const QString &system, const QString &user, const QJsonObject &schema, int maxTokens, int timeoutMs, Done done)
{
    if (m_done) {
        done(false, {}, u"busy"_s);
        return;
    }
    if (!installed()) {
        done(false, {}, unavailableReason(false));
        return;
    }
    m_done = std::move(done);
    m_timeoutMs = timeoutMs;
    QJsonObject body{{u"messages"_s, QJsonArray{QJsonObject{{u"role"_s, u"system"_s}, {u"content"_s, system}},
                                                 QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, user}}}},
                     {u"temperature"_s, 0},
                     {u"max_tokens"_s, maxTokens},
                     {u"cache_prompt"_s, true},
                     {u"chat_template_kwargs"_s, QJsonObject{{u"enable_thinking"_s, false}}}};
    if (!schema.isEmpty())
        body[u"response_format"_s] = QJsonObject{{u"type"_s, u"json_schema"_s},
                                                  {u"json_schema"_s, QJsonObject{{u"name"_s, u"plan"_s}, {u"schema"_s, schema}}}};
    m_body = body;
    m_idle.stop();
    if (m_server == Server::Ready) {
        send();
        return;
    }
    // A crash a moment ago is not retried in a loop: report it.
    if (m_server == Server::Failed && m_crashes >= 2 && now() - m_lastCrash < 60000) {
        finishRequest(false, {}, u"crashed: "_s + m_lastError);
        return;
    }
    m_timeout.start(kStartTimeoutMs);
    if (m_server != Server::Starting)
        start();
}

void LocalModel::abort()
{
    if (m_reply)
        m_reply->abort();
    finishRequest(false, {}, u"aborted"_s);
}

void LocalModel::stop()
{
    m_health.stop();
    m_idle.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process.terminate();
        if (!m_process.waitForFinished(3000))
            m_process.kill();
        m_process.waitForFinished(1000);
        m_stopping = false;
    }
    if (m_probe.state() != QProcess::NotRunning)
        m_probe.kill();
    if (m_server != Server::Failed)
        m_server = Server::Stopped;
    emit changed();
}

void LocalModel::start()
{
    m_server = Server::Starting;
    m_lastError.clear();
    m_startClock.start();
    emit changed();
    if (m_device == u"cpu") {
        launch(u"none"_s);
        return;
    }
    probeDevices();
}

// llama.cpp lists the GPUs it can use; on a hybrid laptop the integrated one often
// reports more (shared) memory, so the discrete card is found by its PCI vendor.
void LocalModel::probeDevices()
{
    m_probe.disconnect(this);
    connect(&m_probe, &QProcess::finished, this, [this] {
        const QString out = QString::fromUtf8(m_probe.readAll());
        struct Dev { QString id, name; int freeMb; };
        QList<Dev> devs;
        static const QRegularExpression line(u"^\\s*(\\w+\\d+):\\s*(.+?)\\s*\\((\\d+) MiB,\\s*(\\d+) MiB free\\)"_s,
                                             QRegularExpression::MultilineOption);
        for (auto it = line.globalMatch(out); it.hasNext();) {
            const auto m = it.next();
            devs.append({m.captured(1), m.captured(2), m.captured(4).toInt()});
        }
        QString pick;
        int freeMb = 0;
        const QList<GpuDevice> gpus = detectGpus();
        QString bootVendor, discreteVendor;
        for (const GpuDevice &g : gpus)
            (g.boot ? bootVendor : discreteVendor) = gpuVendorName(g.vendorId);
        auto vendorOf = [](const QString &name) {
            const QString n = name.toLower();
            return n.contains(u"nvidia"_s) ? u"NVIDIA"_s : (n.contains(u"amd"_s) || n.contains(u"radeon"_s)) ? u"AMD"_s
                                         : n.contains(u"intel"_s) ? u"Intel"_s : QString();
        };
        if (!discreteVendor.isEmpty() && discreteVendor != bootVendor)
            for (const Dev &d : devs)
                if (pick.isEmpty() && vendorOf(d.name) == discreteVendor) {
                    pick = d.id;
                    freeMb = d.freeMb;
                }
        if (pick.isEmpty() && devs.size() == 1) {
            pick = devs.first().id;
            freeMb = devs.first().freeMb;
        }
        const Entry e = entry(m_modelId);
        // Not enough room on the card: the processor does it (slower, still offline).
        if (pick.isEmpty() || (m_device == u"auto" && e.vramMb > 0 && freeMb < e.vramMb))
            pick = u"none"_s;
        launch(pick);
    });
    m_probe.setProcessChannelMode(QProcess::MergedChannels);
    m_probe.start(serverProgram(), {u"--list-devices"_s});
    QTimer::singleShot(15000, &m_probe, [this] {
        if (m_probe.state() != QProcess::NotRunning)
            m_probe.kill();
    });
}

void LocalModel::launch(const QString &device)
{
    if (m_server != Server::Starting)
        return; // stopped meanwhile
    m_chosenDevice = device == u"none" ? u"cpu"_s : device;
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        m_port = probe.serverPort();
    }
    const Entry e = entry(m_modelId);
    QStringList args{u"-m"_s, modelsDir() + u'/' + e.file, u"--host"_s, u"127.0.0.1"_s, u"--port"_s, QString::number(m_port),
                     u"-c"_s, QString::number(kContext), u"-np"_s, u"1"_s, u"--jinja"_s, u"--reasoning-budget"_s, u"0"_s,
                     u"--no-webui"_s, u"-t"_s, QString::number(std::max(2, QThread::idealThreadCount() / 2))};
    if (device == u"none")
        args << u"-ngl"_s << u"0"_s << u"--device"_s << u"none"_s;
    else
        args << u"-ngl"_s << u"99"_s << u"--device"_s << device;
    qInfo().noquote() << "local model: starting llama-server on" << m_chosenDevice << "port" << m_port;
    // The GPU drivers keep compiled shaders in a cache; jarvisd's home is read-only except
    // its data folder, so the cache lives there — otherwise every start compiles them again
    // (tens of seconds on the first request).
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/jarvis/cache"_s;
    QDir().mkpath(cache + u"/nvidia"_s);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"XDG_CACHE_HOME"_s, cache);
    env.insert(u"__GL_SHADER_DISK_CACHE_PATH"_s, cache + u"/nvidia"_s);
    env.insert(u"__GL_SHADER_DISK_CACHE_SKIP_CLEANUP"_s, u"1"_s);
    m_process.setProcessEnvironment(env);
    m_process.start(serverProgram(), args);
    m_health.start();
}

void LocalModel::pollHealth()
{
    if (m_server != Server::Starting || m_process.state() == QProcess::NotRunning) {
        m_health.stop();
        return;
    }
    QNetworkRequest req(QUrl(u"http://127.0.0.1:%1/health"_s.arg(m_port)));
    req.setTransferTimeout(1000);
    QNetworkReply *r = m_net.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (m_server != Server::Starting || !m_health.isActive())
            return;
        if (r->error() == QNetworkReply::NoError && r->readAll().contains("ok")) {
            m_health.stop();
            warmUp();
        }
    });
}

void LocalModel::warmUp()
{
    // One token through the whole model: the GPU compiles its kernels now, not on the user's request.
    QNetworkRequest req(QUrl(u"http://127.0.0.1:%1/v1/chat/completions"_s.arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    req.setTransferTimeout(kStartTimeoutMs);
    QJsonObject body{{u"messages"_s, QJsonArray{QJsonObject{{u"role"_s, u"system"_s}, {u"content"_s, m_warmSystem}},
                                                 QJsonObject{{u"role"_s, u"user"_s}, {u"content"_s, u"DATA:\nПросьба пользователя: громкость 30"_s}}}},
                     {u"max_tokens"_s, 8},
                     {u"temperature"_s, 0},
                     {u"cache_prompt"_s, true},
                     {u"chat_template_kwargs"_s, QJsonObject{{u"enable_thinking"_s, false}}}};
    if (!m_warmSchema.isEmpty())
        body[u"response_format"_s] = QJsonObject{{u"type"_s, u"json_schema"_s},
                                                  {u"json_schema"_s, QJsonObject{{u"name"_s, u"plan"_s}, {u"schema"_s, m_warmSchema}}}};
    QNetworkReply *r = m_net.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (m_server != Server::Starting)
            return;
        m_server = Server::Ready;
        m_crashes = 0;
        m_lastStartMs = m_startClock.elapsed();
        qInfo().noquote() << "local model ready in" << m_lastStartMs << "ms on" << m_chosenDevice;
        emit changed();
        if (m_done)
            send();
        else
            m_idle.start(m_keepMinutes * 60000);
    });
}

void LocalModel::send()
{
    m_timeout.start(m_timeoutMs);
    m_requestClock.start();
    QNetworkRequest req(QUrl(u"http://127.0.0.1:%1/v1/chat/completions"_s.arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    m_reply = m_net.post(req, QJsonDocument(m_body).toJson(QJsonDocument::Compact));
    QNetworkReply *r = m_reply;
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r != m_reply)
            return;
        m_reply = nullptr;
        const QByteArray data = r->readAll();
        if (r->error() != QNetworkReply::NoError) {
            finishRequest(false, {}, r->errorString());
            return;
        }
        m_lastRequestMs = m_requestClock.elapsed();
        const QString content = QJsonDocument::fromJson(data)
                                    .object()
                                    .value(u"choices"_s)
                                    .toArray()
                                    .at(0)
                                    .toObject()
                                    .value(u"message"_s)
                                    .toObject()
                                    .value(u"content"_s)
                                    .toString();
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(content.trimmed().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            finishRequest(false, {}, u"the model's answer is not a JSON object"_s);
            return;
        }
        finishRequest(true, doc.object(), {});
    });
}

void LocalModel::finishRequest(bool ok, const QJsonObject &answer, const QString &error)
{
    m_timeout.stop();
    if (m_server == Server::Ready)
        m_idle.start(m_keepMinutes * 60000);
    Done done = std::exchange(m_done, nullptr);
    if (done)
        done(ok, answer, error);
}

void LocalModel::serverDied(int code, QProcess::ExitStatus status)
{
    m_health.stop();
    if (m_stopping)
        return;
    ++m_crashes;
    m_lastCrash = now();
    m_server = Server::Failed;
    if (m_lastError.isEmpty() || status == QProcess::CrashExit)
        m_lastError = status == QProcess::CrashExit ? u"llama-server crashed"_s : u"llama-server exited with code %1"_s.arg(code);
    qWarning().noquote() << "local model:" << m_lastError;
    if (m_reply)
        m_reply->abort();
    finishRequest(false, {}, u"crashed: "_s + m_lastError);
    emit changed();
}

} // namespace jarvis
