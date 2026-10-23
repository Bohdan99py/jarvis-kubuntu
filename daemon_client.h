#pragma once

#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

// GUI-side proxy of jarvisd. Tracks whether the daemon is on the bus and
// sends one request at a time. Never blocks the UI thread.
class DaemonClient : public QObject
{
    Q_OBJECT

public:
    explicit DaemonClient(QObject *parent = nullptr);

    bool isAvailable() const noexcept { return m_available; }

    // Asks the bus to start the daemon via D-Bus activation (no-op if there
    // is no installed .service file). Asynchronous.
    void tryStartDaemon();

    // `desktop`: JSON of the desktop context ({screen, screenText}); sent with AskContext.
    void ask(const QString &text, const QStringList &files = {}, const QString &desktop = {});
    // Asks the daemon to stop the answer being written.
    void cancel();
    // Stops waiting: a late answer is ignored.
    void abandon();
    bool waiting() const noexcept { return m_waiting; }

    // Tell the daemon to re-read its config file (fire and forget).
    void reloadConfig();
    // Ask the daemon to exit. Blocks for up to one second so the request is
    // really on the bus before the GUI quits.
    void quitDaemon();

signals:
    void availabilityChanged(bool available);
    void replyReady(const QString &reply);
    // The answer so far, while it is written.
    void replyProgress(const QString &textSoFar);
    void failed(const QString &reason);
    // What the daemon is doing, for the interface: the route of a request
    // (`own`: this window asked) and Claude failures and notifications.
    void routed(const QString &route, bool own);
    void claudeFailed(const QString &reason, bool own);
    void notice(const QString &kind, const QString &title);
    // An offer of help (JSON: category, title, body, score) and a context change.
    void suggestion(const QString &json);
    void contextChanged();

private slots:
    void onReplySignal(quint64 id, const QString &reply);
    void onProgressSignal(quint64 id, const QString &text);
    void onRoutedSignal(quint64 id, const QString &route);
    void onFailedSignal(quint64 id, const QString &reason);
    void onNoticeSignal(const QString &json);

private:
    void setAvailable(bool available);
    void fail(const QString &reason);

    static constexpr int kCallTimeoutMs = 3000;
    // Silence this long ends the wait; every piece of a streaming answer restarts it.
    static constexpr int kReplyTimeoutMs = 120000;

    QDBusServiceWatcher m_watcher;
    QTimer m_timeout;
    quint64 m_pendingId = 0;
    bool m_waiting = false;
    bool m_available = false;
};
