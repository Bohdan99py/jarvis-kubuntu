#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QString>

#include "chat_engine.h"

namespace jarvis {

// The daemon's brain: owns the engine and publishes it on the session bus.
// Future engines (context, memory, AI router) are added here, next to ChatEngine.
class DaemonService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DaemonService)

public:
    static constexpr int kMaxRequestChars = 4096;

    explicit DaemonService(QObject *parent = nullptr);
    ~DaemonService() override = default;

    // Registers object + well-known name on the session bus.
    bool start(QString *error);

    // Returns the request id, or 0 if the request was rejected.
    quint64 ask(const QString &text);

    static QString version();

signals:
    void replyReady(quint64 id, const QString &reply);

private:
    ChatEngine m_engine;
};

// D-Bus face of DaemonService: interface org.jarvis.Daemon1
//   method Ask(s) -> t      queue a request, returns its id (0 = rejected)
//   method Version() -> s
//   signal Reply(t, s)      answer for request id
class DaemonAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jarvis.Daemon1")

public:
    explicit DaemonAdaptor(DaemonService *service);

public slots:
    quint64 Ask(const QString &text);
    QString Version();

signals:
    void Reply(quint64 id, const QString &text);

private:
    DaemonService *m_service;
};

} // namespace jarvis
