#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QString>
#include <QStringList>

#include "jv_sys.h"
#include "language.h"

// The KDE integration bridge (0.19): what jarvisd offers Plasma — the desktop
// widget, KRunner, scripts — over org.jarvis.Integration1, and the rules that
// keep it safe. Pure logic, no D-Bus: jarvisd and the GUI use it, tests check it.
//
// Privilege tiers (the session bus is per user; every caller runs as the user):
//   observe   Status, Actions, SystemMetrics — nothing personal: Jarvis's state,
//             the kind and title of the last notification (already on screen),
//             load figures. Any process.
//   act       RunAction, OpenOverlay, Notify — only the actions listed by
//             actions(), no arguments, no commands, rate limited per caller.
//             Off together with the integration switch.
//   personal  ActiveApplication — the focused application's name, never its
//             title; only with the user's "share the active application"
//             consent on top of window awareness.
// What stays out of reach: AskContext and ScreenObserved (the Jarvis window
// only), WindowFocused (KWin only), the configuration and the memory.
namespace jarvis::integration {

inline constexpr QLatin1String kPath{"/org/jarvis/Integration1"};
inline constexpr QLatin1String kInterface{"org.jarvis.Integration1"};
inline constexpr QLatin1String kRunnerPath{"/org/jarvis/KRunner"};
// The Plasma widget (plasmoid/metadata.json).
inline constexpr QLatin1String kHudPlugin{"org.jarvis.hud"};

// Actions a desktop component may start: [{id, label, icon}].
// overlay, main, quick, tasks, memory open a part of Jarvis; pause-awareness
// (one hour), resume-awareness and cancel (the answer being written) act on the daemon.
QJsonArray actions(Lang lang);
bool isAction(const QString &id);
// The DesktopController target an opening action shows ("" for daemon actions).
QString actionTarget(const QString &id);

// A token bucket per caller: `burst` calls at once, then one every `refillMs`.
// Keeps at most 64 callers (the oldest are forgotten).
class RateLimiter
{
public:
    RateLimiter(int burst, qint64 refillMs);
    bool allow(const QString &caller, qint64 nowMs);

private:
    struct Bucket
    {
        double tokens;
        qint64 at;
    };
    QHash<QString, Bucket> m_buckets;
    int m_burst;
    qint64 m_refillMs;
};

// {cpu, cores, memory, swap, load, temperature, battery: {present, percent,
// charging, onBattery}, uptime}. Percentages are -1 when unknown; `prev` may be
// null (no CPU figure yet).
QJsonObject systemMetrics(const jv_cpu_times *prev, const jv_cpu_times *now);
bool onBattery(const QJsonObject &metrics);

// The desktop HUD's animation intensity (Config::hudAnimationLevels()).
// On battery the HUD steps down one level (high -> normal -> low); off stays off.
QString effectiveAnimation(const QString &wanted, bool onBattery);

// JARVIS DESKTOP settings (config.json "desktop"), every key present and in range:
//   mode         standard | jarvis          (what the desktop is now; the shell sets it)
//   theme        classic | mark7 | minimal
//   primary, secondary  "" (the theme's) or "#rrggbb"
//   glow 0..1.5, opacity 0.3..0.95, motion 0.5..1.5, density compact | normal | comfortable
//   quality      low | balanced | cinematic (the desktop core and widgets)
//   coreSize     small | medium | large;  corePosition center | left | right
//   dockPosition bottom | top | left | right; dockSize 32..72 px; dockAutohide; dockAllScreens
//   replacePanels  remove the standard panels once the Jarvis dock works (they come back with a restore)
//   effects      widget animations at all
QJsonObject normalizeDesktop(const QJsonObject &in);
// Applies one change from the Control center; "" or why it was refused.
QString setDesktopValue(QJsonObject &desktop, const QString &key, const QJsonValue &value);

// KRunner: the question after a trigger word ("jarvis what is RC filter",
// "джарвис …"), or "" when the query is not for Jarvis.
QStringList runnerTriggers();
QString runnerQuestion(const QString &query);

// Plasma desktop scripting (org.kde.PlasmaShell.evaluateScript). The scripts
// touch only widgets whose plugin is kHudPlugin: panels, other widgets and
// wallpapers are never read or changed. Each prints one number.
QString plasmaCountScript();
// Adds one widget (a Jarvis widget id; default the core) to the desktop of the
// first screen, top right, unless one exists. "" for an id that is not Jarvis's.
QString plasmaAddScript(const QString &type = QString(kHudPlugin));
// Removes the Jarvis widgets of `type` ("" = every Jarvis widget) from desktops and panels.
QString plasmaRemoveScript(const QString &type = QString(kHudPlugin));

// A systemd user unit as the diagnostics see it.
struct UnitHealth
{
    QString unit;
    QString load;      // loaded, not-found, masked…
    QString active;    // active, inactive, failed, activating…
    QString sub;       // running, dead, auto-restart…
    QString result;    // success, exit-code, watchdog, start-limit-hit…
    QString fileState; // enabled, disabled, static, masked…
    uint restarts = 0;
    quint64 memoryBytes = 0;
    quint64 cpuNs = 0;
};
// {unit, level: ok|warn|error|off, text}. `wanted`: the user expects it to run.
QJsonObject assessUnit(const UnitHealth &u, bool wanted, Lang lang);

} // namespace jarvis::integration
