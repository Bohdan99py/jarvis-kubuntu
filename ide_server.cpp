#include "ide_server.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>

#include <sys/socket.h>
#include <unistd.h>

#include "assistant.h"
#include "build_config.h"
#include "dbus_names.h"

using namespace Qt::StringLiterals;

namespace jarvis {
namespace {

bool samePeerUser(QLocalSocket *socket)
{
    struct ucred cred {};
    socklen_t length = sizeof cred;
    const int fd = int(socket->socketDescriptor());
    return fd >= 0 && getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &length) == 0 && cred.uid == getuid();
}

QStringList strings(const QJsonValue &value, int max, int maxLength)
{
    QStringList out;
    for (const auto &v : value.toArray()) {
        if (out.size() >= max)
            break;
        if (v.isString())
            out.append(v.toString().left(maxLength));
    }
    return out;
}

} // namespace

IdeServer::IdeServer(Assistant *assistant, QString path, QObject *parent)
    : QObject(parent)
    , m_assistant(assistant)
    , m_path(std::move(path))
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(kMaxClients);
    connect(&m_server, &QLocalServer::newConnection, this, &IdeServer::accept);
    connect(m_assistant, &Assistant::replyReady, this, &IdeServer::onReply);
}

IdeServer::~IdeServer()
{
    m_server.close();
    QFile::remove(m_path);
}

QString IdeServer::defaultPath(const QString &service)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    // A development daemon (JARVIS_DBUS_SERVICE) gets its own socket.
    return dir + (service == QLatin1String(dbus::kService) ? u"/jarvis-ide.sock"_s
                                                           : u"/jarvis-ide-"_s + service + u".sock"_s);
}

bool IdeServer::start(QString *error)
{
    // The bus name is unique, so a socket file left here belongs to a dead daemon.
    QLocalServer::removeServer(m_path);
    if (!m_server.listen(m_path)) {
        *error = m_server.errorString();
        return false;
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    qInfo().noquote() << "IDE socket ready:" << m_path;
    return true;
}

void IdeServer::accept()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        if (m_clients.size() >= kMaxClients || !samePeerUser(socket)) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_clients.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
            updateCount();
        });
        updateCount();
    }
}

void IdeServer::updateCount()
{
    m_assistant->setIdeClients(int(m_clients.size()));
}

void IdeServer::read(QLocalSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    it->buffer += socket->readAll();
    if (it->buffer.size() > kMaxLine && !it->buffer.contains('\n')) {
        qWarning() << "IDE client sent an oversized message; disconnecting";
        socket->abort();
        return;
    }
    int end;
    while ((end = it->buffer.indexOf('\n')) >= 0) {
        const QByteArray line = it->buffer.left(end);
        it->buffer.remove(0, end + 1);
        if (line.size() > kMaxLine)
            continue;
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        if (!message.isEmpty())
            handle(socket, message);
        it = m_clients.find(socket); // handle() may have dropped the client
        if (it == m_clients.end())
            return;
    }
}

void IdeServer::send(QLocalSocket *socket, const QJsonObject &message)
{
    if (socket && socket->state() == QLocalSocket::ConnectedState)
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void IdeServer::handle(QLocalSocket *socket, const QJsonObject &m)
{
    Client &client = m_clients[socket];
    const QString type = m.value(u"type"_s).toString();
    const QJsonValue id = m.value(u"id"_s);

    if (type == u"hello") {
        send(socket, {{u"type"_s, u"hello"_s}, {u"id"_s, id}, {u"version"_s, QStringLiteral(JARVIS_VERSION)},
                      {u"claude"_s, m_assistant->claudeEnabled()}});
    } else if (type == u"ask") {
        if (client.pending >= kMaxPendingPerClient) {
            send(socket, {{u"type"_s, u"reply"_s}, {u"id"_s, id}, {u"ok"_s, false}, {u"text"_s, u"busy"_s}});
            return;
        }
        const quint64 request = m_assistant->askCode(m.value(u"mode"_s).toString(),
                                                     m.value(u"text"_s).toString().left(4096),
                                                     m.value(u"context"_s).toObject());
        if (request == 0) {
            send(socket, {{u"type"_s, u"reply"_s}, {u"id"_s, id}, {u"ok"_s, false}, {u"text"_s, u"rejected"_s}});
            return;
        }
        ++client.pending;
        m_requests.insert(request, {socket, id});
    } else if (type == u"teach") {
        const QString error = m_assistant->teachCode(m.value(u"language"_s).toString(), m.value(u"problem"_s).toString(),
                                                     m.value(u"solution"_s).toString());
        send(socket, {{u"type"_s, u"result"_s}, {u"id"_s, id}, {u"ok"_s, error.isEmpty()}, {u"text"_s, error}});
    } else if (type == u"rate") {
        const QString text = m_assistant->rateCode(quint64(m.value(u"request"_s).toDouble()), m.value(u"good"_s).toBool());
        send(socket, {{u"type"_s, u"result"_s}, {u"id"_s, id}, {u"ok"_s, true}, {u"text"_s, text}});
    } else if (type == u"activity") {
        m_assistant->codeActivity(m.value(u"language"_s).toString().left(40), m.value(u"project"_s).toString().left(80));
    } else if (type == u"workspace") {
        m_assistant->codeWorkspace(m.value(u"project"_s).toString().left(80), strings(m.value(u"languages"_s), 10, 40),
                                   strings(m.value(u"frameworks"_s), 20, 40));
    } else if (type == u"diagnostic") {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - client.window > 60 * 1000) {
            client.window = now;
            client.diagnostics = 0;
        }
        if (++client.diagnostics <= kMaxDiagnosticsPerMinute)
            m_assistant->codeDiagnostic(m.value(u"language"_s).toString().left(40),
                                        m.value(u"project"_s).toString().left(80),
                                        m.value(u"message"_s).toString().left(500));
    }
}

void IdeServer::onReply(quint64 request, const QString &text)
{
    const auto it = m_requests.find(request);
    if (it == m_requests.end())
        return; // a chat reply, not ours
    const Pending pending = *it;
    m_requests.erase(it);
    if (!pending.socket)
        return;
    auto client = m_clients.find(pending.socket.data());
    if (client != m_clients.end())
        client->pending = std::max(0, client->pending - 1);
    send(pending.socket, {{u"type"_s, u"reply"_s}, {u"id"_s, pending.id}, {u"request"_s, double(request)},
                          {u"ok"_s, true}, {u"text"_s, text}});
}

} // namespace jarvis
