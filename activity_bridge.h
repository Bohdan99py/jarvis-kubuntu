#pragma once

#include <QObject>
#include <QString>

namespace jarvis {

class Assistant;

// Connects the desktop to the assistant (jarvisd only):
//  * loads a KWin script that reports the focused window while tracking is on
//    (Plasma 5 and 6, X11 and Wayland) and unloads it when tracking is off or
//    the daemon exits, so KWin never re-activates a stopped daemon;
//  * pauses accounting while the screen is locked.
class ActivityBridge : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ActivityBridge)

public:
    ActivityBridge(Assistant *assistant, QString service, QObject *parent = nullptr);
    ~ActivityBridge() override;

    void apply(bool enabled);

private slots:
    void onScreenSaverActive(bool active);

private:
    bool load(QString *error);
    void unload();

    Assistant *m_assistant;
    QString m_service;
    bool m_loaded = false;
};

} // namespace jarvis
