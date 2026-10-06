#pragma once

#include <QObject>
#include <QString>

#include "system_info.h"

namespace jarvis {

// Deterministic local brain: greetings, small talk, time/date and live system
// reports from the C layer. No network, no state. The Assistant decides when
// to use it and when to hand a message to Claude.
class ChatEngine : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ChatEngine)

public:
    enum class Match {
        None,      // nothing recognised; reply holds a generic hint
        SmallTalk, // greetings, "how are you", thanks...
        Data,      // answers built from live data (system reports, time, date)
    };

    explicit ChatEngine(QObject *parent = nullptr);
    ~ChatEngine() override = default;

    Match match(const QString &input, QString *reply) const;
    QString respond(const QString &input) const;

    static bool looksRussian(const QString &text);

private:
    SystemInfo m_sys;
};

} // namespace jarvis
