#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

#include "agent_system.h"

namespace jarvis {

// The agent's windows (0.22), in jarvisd: a one-shot KWin script
// (kwin/windows.js) lists the windows, activates one or asks one to close,
// and reports back through Integration1.WorkspaceReport (accepted from KWin
// only). The agent calls it from its worker thread and waits at most a few
// seconds; the script is loaded and unloaded on the daemon's own thread.
class WindowControl : public QObject
{
    Q_OBJECT

public:
    explicit WindowControl(QObject *parent = nullptr);

    // Worker thread: blocks until KWin answers or 4 s pass.
    QList<agent::Window> windows(QString *error);
    QString act(const QString &mode, const QString &id);

    // The daemon's thread: a WorkspaceReport whose mode starts with "windows:".
    // True when it was ours.
    bool report(const QJsonObject &o);

private:
    QJsonObject run(const QString &mode, const QString &id, QString *error);
    Q_INVOKABLE QString load(const QString &mode, const QString &id, const QString &token);

    QMutex m_mutex;
    QWaitCondition m_answered;
    QString m_token;
    QJsonObject m_answer;
    QMutex m_serial; // one script at a time
};

} // namespace jarvis
