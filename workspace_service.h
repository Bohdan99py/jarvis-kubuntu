#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

#include "language.h"
#include "workspace.h"

namespace jarvis {

class Assistant;
class TelemetryService;

// The workspace manager (0.19.2), in jarvisd. A switch:
//  1. remembers the previous profile and tells awareness the new one;
//  2. loads a one-shot KWin script (kwin/workspace.js) that moves the profile's
//     windows to its virtual desktop, puts them where the user saved them,
//     switches the desktop and reports which applications run;
//  3. asks the Jarvis window to start the profile's applications that do not
//     run (jarvisd's sandbox is no place to start applications from) and, for
//     "prepare", to open its files and projects.
// Plus the desktop commands of the chat, the voice and KRunner.
class WorkspaceService : public QObject
{
    Q_OBJECT

public:
    // Calls a method of the Jarvis window (org.jarvis.Desktop1), starting it if needed.
    using GuiCall = std::function<void(const QString &method, const QVariantList &args)>;
    WorkspaceService(Assistant *assistant, TelemetryService *telemetry, GuiCall gui, QObject *parent = nullptr);

    // "" on success, otherwise why not; `message` gets the reply for people.
    QString switchTo(const QString &id, bool prepare, Lang lang, QString *message = nullptr);
    QString previous(Lang lang, QString *message = nullptr);
    QString saveLayout(Lang lang, QString *message = nullptr);
    // The KWin script's answer (accepted from KWin only, by the caller).
    void report(const QString &json);
    QJsonObject summary() const;
    // A chat, voice or KRunner command; "" when it is not one this service can do.
    QString handle(const workspace::Command &command, Lang lang);
    // Projects found under the home folder (cached; a scan starts in a worker thread when old).
    QList<workspace::Project> projects(const QString &words);
    // Applications of the current profile that the dock or a widget may show.
    workspace::Profile current() const { return workspace::find(m_state, m_state.current); }

signals:
    void changed();
    void event(const QString &kind, const QString &text);

private:
    bool runScript(const QString &mode, const workspace::Profile &profile, QString *error);
    void scanProjects();

    Assistant *m_assistant;
    TelemetryService *m_telemetry;
    GuiCall m_gui;
    workspace::State m_state;
    QString m_token;
    QString m_pendingMode;
    bool m_pendingPrepare = false;
    QList<workspace::Project> m_projects;
    qint64 m_projectsAt = 0;
    bool m_scanning = false;
    mutable QString m_appliedActivity; // the workspace activity level the assistant has
};

} // namespace jarvis
