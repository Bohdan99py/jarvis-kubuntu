#pragma once

#include <QObject>
#include <QString>

#include "system_info.h"

namespace jarvis {

// Headless, UI-independent brain. Small rule-based responder (greetings,
// small talk, time/date) plus live system reports from the C layer.
// The public surface (ask -> replyReady) is what an LLM backend or a D-Bus
// proxy will implement later.
class ChatEngine : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ChatEngine)

public:
    explicit ChatEngine(QObject *parent = nullptr);
    ~ChatEngine() override = default;

    // Synchronous: easy to unit-test.
    QString respond(const QString &input) const;

    // Asynchronous: returns a request id and emits replyReady(id, ...) after
    // a short "thinking" delay. Ids let several clients share one engine.
    quint64 ask(const QString &input);

signals:
    void replyReady(quint64 id, const QString &reply);

private:
    SystemInfo m_sys;
    quint64 m_nextId = 0;
};

} // namespace jarvis
