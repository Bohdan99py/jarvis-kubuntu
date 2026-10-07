#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QString>

class QLocalSocket;

namespace jarvis {

class Assistant;

// Local socket for editor integrations (the VS Code extension), jarvisd only.
// ${XDG_RUNTIME_DIR}/jarvis-ide.sock, owner-only, peers checked with
// SO_PEERCRED. One JSON object per line in both directions:
//   → {"type":"hello"}                                   ← {"type":"hello","version","claude"}
//   → {"type":"ask","id","mode","text","context"}        ← {"type":"reply","id","request","ok","text"}
//   → {"type":"teach","id","language","problem","solution"}  ← {"type":"result","id","ok","text"}
//   → {"type":"rate","id","request","good"}              ← {"type":"result","id","ok","text"}
//   → {"type":"activity","language","project"}           (heartbeat, no reply)
//   → {"type":"workspace","project","languages","frameworks"}
//   → {"type":"diagnostic","language","project","message"}
class IdeServer : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(IdeServer)

public:
    static constexpr int kMaxClients = 8;
    static constexpr int kMaxLine = 256 * 1024;
    static constexpr int kMaxPendingPerClient = 4;
    static constexpr int kMaxDiagnosticsPerMinute = 30;

    IdeServer(Assistant *assistant, QString path, QObject *parent = nullptr);
    ~IdeServer() override;

    bool start(QString *error);
    static QString defaultPath(const QString &service);

private:
    struct Client
    {
        QByteArray buffer;
        int pending = 0;
        int diagnostics = 0;
        qint64 window = 0;
    };

    void accept();
    void read(QLocalSocket *socket);
    void handle(QLocalSocket *socket, const QJsonObject &message);
    void send(QLocalSocket *socket, const QJsonObject &message);
    void onReply(quint64 request, const QString &text);
    void updateCount();

    Assistant *m_assistant;
    QString m_path;
    QLocalServer m_server;
    QHash<QLocalSocket *, Client> m_clients;
    struct Pending
    {
        QPointer<QLocalSocket> socket;
        QJsonValue id;
    };
    QHash<quint64, Pending> m_requests;
};

} // namespace jarvis
