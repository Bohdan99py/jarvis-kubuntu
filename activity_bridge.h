#pragma once

#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

namespace jarvis {

class Assistant;

// Connects the desktop to the assistant (jarvisd only):
//  * loads a KWin script that reports the focused window while tracking is on
//    (Plasma 5 and 6, X11 and Wayland) and unloads it when tracking is off or
//    the daemon exits, so KWin never re-activates a stopped daemon;
//  * waits for KWin when the daemon starts before the Plasma session is ready
//    and reloads the script when KWin restarts;
//  * accepts window reports only from the bus connection that owns org.kde.KWin;
//  * pauses accounting while the screen is locked.
class ActivityBridge : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ActivityBridge)

public:
    ActivityBridge(Assistant *assistant, QString service, QObject *parent = nullptr);
    ~ActivityBridge() override;

    void apply(bool enabled);
    // True when `sender` (a unique bus name) is the current owner of org.kde.KWin.
    bool isKWin(const QString &sender) const;

private slots:
    void onScreenSaverActive(bool active);

private:
    bool load(QString *error);
    void unload();
    void tryLoad();

    Assistant *m_assistant;
    QString m_service;
    QDBusServiceWatcher m_kwinWatcher;
    QTimer m_retry;
    int m_attempts = 0;
    bool m_enabled = false;
    bool m_loaded = false;
};

} // namespace jarvis
