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

    void ask(const QString &text);

    // Tell the daemon to re-read its config file (fire and forget).
    void reloadConfig();
    // Ask the daemon to exit. Blocks for up to one second so the request is
    // really on the bus before the GUI quits.
    void quitDaemon();

signals:
    void availabilityChanged(bool available);
    void replyReady(const QString &reply);
    void failed(const QString &reason);

private slots:
    void onReplySignal(quint64 id, const QString &reply);

private:
    void setAvailable(bool available);
    void fail(const QString &reason);

    static constexpr int kCallTimeoutMs = 3000;
    static constexpr int kReplyTimeoutMs = 90000; // Claude may take up to 60 s

    QDBusServiceWatcher m_watcher;
    QTimer m_timeout;
    quint64 m_pendingId = 0;
    bool m_waiting = false;
    bool m_available = false;
};
