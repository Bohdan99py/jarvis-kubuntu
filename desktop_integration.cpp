#include "desktop_integration.h"

#include <QRegularExpression>

#include <algorithm>

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace jarvis::integration {
namespace {

QString pick(Lang lang, const QString &ru, const QString &en)
{
    return lang == Lang::Ru ? ru : en;
}

struct ActionDef
{
    const char *id;
    const char *target;
    const char *icon;
};

// Opening actions show a part of the Jarvis window; the others act on the daemon.
constexpr ActionDef kActions[] = {
    {"overlay", "overlay", "org.jarvis.Jarvis"},
    {"main", "main", "window"},
    {"quick", "quick", "system-search"},
    {"tasks", "tasks", "view-task"},
    {"memory", "memory", "brain"},
    {"pause-awareness", "", "media-playback-pause"},
    {"resume-awareness", "", "media-playback-start"},
    {"cancel", "", "process-stop"},
};

QString actionLabel(const QString &id, Lang lang)
{
    if (id == u"overlay")
        return pick(lang, u"Открыть оверлей"_s, u"Open the overlay"_s);
    if (id == u"main")
        return pick(lang, u"Открыть Jarvis"_s, u"Open Jarvis"_s);
    if (id == u"quick")
        return pick(lang, u"Быстрые команды"_s, u"Quick commands"_s);
    if (id == u"tasks")
        return pick(lang, u"Задачи и напоминания"_s, u"Tasks and reminders"_s);
    if (id == u"memory")
        return pick(lang, u"Память"_s, u"Memory"_s);
    if (id == u"pause-awareness")
        return pick(lang, u"Пауза осведомлённости на час"_s, u"Pause awareness for an hour"_s);
    if (id == u"resume-awareness")
        return pick(lang, u"Возобновить осведомлённость"_s, u"Resume awareness"_s);
    if (id == u"cancel")
        return pick(lang, u"Остановить ответ"_s, u"Stop the answer"_s);
    return id;
}

double round1(double v)
{
    return std::round(v * 10) / 10;
}

QString hudCollector()
{
    // Every containment that can hold a widget: desktops (one per screen and
    // activity) and panels. Nothing else is touched.
    return u"var all = desktops().concat(panels());\n"
           u"function jarvisWidgets(c) { return c.widgets(\"%1\"); }\n"_s.arg(kHudPlugin);
}

} // namespace

QJsonArray actions(Lang lang)
{
    QJsonArray out;
    for (const ActionDef &a : kActions) {
        const QString id = QString::fromLatin1(a.id);
        out.append(QJsonObject{{u"id"_s, id}, {u"label"_s, actionLabel(id, lang)}, {u"icon"_s, QString::fromLatin1(a.icon)}});
    }
    return out;
}

bool isAction(const QString &id)
{
    return std::any_of(std::begin(kActions), std::end(kActions), [&id](const ActionDef &a) { return id == QLatin1String(a.id); });
}

QString actionTarget(const QString &id)
{
    for (const ActionDef &a : kActions)
        if (id == QLatin1String(a.id))
            return QString::fromLatin1(a.target);
    return {};
}

RateLimiter::RateLimiter(int burst, qint64 refillMs)
    : m_burst(std::max(1, burst))
    , m_refillMs(std::max<qint64>(1, refillMs))
{
}

bool RateLimiter::allow(const QString &caller, qint64 nowMs)
{
    auto it = m_buckets.find(caller);
    if (it == m_buckets.end()) {
        if (m_buckets.size() >= 64) {
            // Forget the caller seen longest ago (its bucket has refilled anyway, most likely).
            auto oldest = std::min_element(m_buckets.begin(), m_buckets.end(),
                                           [](const Bucket &a, const Bucket &b) { return a.at < b.at; });
            m_buckets.erase(oldest);
        }
        it = m_buckets.insert(caller, Bucket{double(m_burst), nowMs});
    }
    Bucket &b = it.value();
    b.tokens = std::min<double>(m_burst, b.tokens + double(std::max<qint64>(0, nowMs - b.at)) / double(m_refillMs));
    b.at = nowMs;
    if (b.tokens < 1)
        return false;
    b.tokens -= 1;
    return true;
}

QJsonObject systemMetrics(const jv_cpu_times *prev, const jv_cpu_times *now)
{
    QJsonObject o;
    o[u"cpu"_s] = prev && now ? round1(jv_cpu_usage_between(prev, now)) : -1.0;
    o[u"cores"_s] = jv_cpu_count();

    jv_meminfo mem{};
    if (jv_mem_read(&mem) == 0 && mem.total_kb > 0) {
        o[u"memory"_s] = round1(100.0 * double(mem.total_kb - std::min(mem.available_kb, mem.total_kb)) / double(mem.total_kb));
        o[u"memoryTotalMb"_s] = double(mem.total_kb / 1024);
        o[u"swap"_s] = mem.swap_total_kb > 0
                           ? round1(100.0 * double(mem.swap_total_kb - std::min(mem.swap_free_kb, mem.swap_total_kb)) / double(mem.swap_total_kb))
                           : 0.0;
    } else {
        o[u"memory"_s] = -1.0;
        o[u"swap"_s] = -1.0;
    }

    jv_loadavg load{};
    o[u"load"_s] = jv_loadavg_read(&load) == 0 ? std::round(load.l1 * 100) / 100 : -1.0;
    double celsius = 0;
    o[u"temperature"_s] = jv_thermal_max_c(&celsius) == 0 ? round1(celsius) : -1.0;
    double uptime = 0;
    o[u"uptime"_s] = jv_uptime_read(&uptime) == 0 ? std::floor(uptime) : -1.0;

    jv_battery bat{};
    QJsonObject battery{{u"present"_s, false}, {u"percent"_s, -1}, {u"charging"_s, false}, {u"onBattery"_s, false}};
    if (jv_battery_read(&bat) == 0 && bat.present) {
        battery[u"present"_s] = true;
        battery[u"percent"_s] = bat.percent;
        battery[u"charging"_s] = bat.charging != 0;
        battery[u"onBattery"_s] = std::strcmp(bat.status, "Discharging") == 0;
    }
    o[u"battery"_s] = battery;
    return o;
}

bool onBattery(const QJsonObject &metrics)
{
    return metrics.value(u"battery"_s).toObject().value(u"onBattery"_s).toBool();
}

QString effectiveAnimation(const QString &wanted, bool battery)
{
    const QStringList levels = Config::hudAnimationLevels();
    const int i = levels.indexOf(wanted);
    if (i < 0)
        return u"normal"_s;
    if (!battery || i <= 1)
        return levels.at(i);
    return levels.at(i - 1);
}

QJsonObject normalizeDesktop(const QJsonObject &in)
{
    const auto choice = [&in](const char *key, const QStringList &allowed) {
        const QString v = in.value(QLatin1String(key)).toString();
        return allowed.contains(v) ? v : allowed.first();
    };
    const auto number = [&in](const char *key, double def, double lo, double hi) {
        const QJsonValue v = in.value(QLatin1String(key));
        return v.isDouble() ? std::clamp(v.toDouble(), lo, hi) : def;
    };
    const auto colour = [&in](const char *key) {
        static const QRegularExpression hex(u"^#[0-9a-fA-F]{6}$"_s);
        const QString v = in.value(QLatin1String(key)).toString();
        return hex.match(v).hasMatch() ? v.toLower() : QString();
    };
    const auto flag = [&in](const char *key, bool def) {
        const QJsonValue v = in.value(QLatin1String(key));
        return v.isBool() ? v.toBool() : def;
    };
    return QJsonObject{{u"mode"_s, choice("mode", {u"standard"_s, u"jarvis"_s})},
                       {u"theme"_s, choice("theme", {u"classic"_s, u"mark7"_s, u"minimal"_s})},
                       {u"primary"_s, colour("primary")},
                       {u"secondary"_s, colour("secondary")},
                       {u"glow"_s, number("glow", 1.0, 0.0, 1.5)},
                       {u"opacity"_s, number("opacity", 0.72, 0.3, 0.95)},
                       {u"motion"_s, number("motion", 1.0, 0.5, 1.5)},
                       {u"density"_s, choice("density", {u"normal"_s, u"compact"_s, u"comfortable"_s})},
                       {u"quality"_s, choice("quality", {u"balanced"_s, u"low"_s, u"cinematic"_s})},
                       {u"coreSize"_s, choice("coreSize", {u"medium"_s, u"small"_s, u"large"_s})},
                       {u"corePosition"_s, choice("corePosition", {u"center"_s, u"left"_s, u"right"_s})},
                       {u"dockPosition"_s, choice("dockPosition", {u"bottom"_s, u"top"_s, u"left"_s, u"right"_s})},
                       {u"dockSize"_s, std::round(number("dockSize", 44, 32, 72))},
                       {u"dockAutohide"_s, flag("dockAutohide", false)},
                       {u"dockAllScreens"_s, flag("dockAllScreens", false)},
                       {u"replacePanels"_s, flag("replacePanels", true)},
                       {u"effects"_s, flag("effects", true)}};
}

QString setDesktopValue(QJsonObject &desktop, const QString &key, const QJsonValue &value)
{
    const QJsonObject before = normalizeDesktop(desktop);
    if (!before.contains(key) || key == u"mode")
        return u"unknown setting"_s; // the mode changes only through the desktop shell
    QJsonObject next = before;
    next[key] = value.isString() ? QJsonValue(value.toString().toLower()) : value;
    next = normalizeDesktop(next);
    // A number out of range is clamped; anything else that does not survive as given is refused.
    if (!value.isDouble() && next.value(key) != (value.isString() ? QJsonValue(value.toString().toLower()) : value))
        return u"invalid value"_s;
    desktop = next;
    return {};
}

QStringList runnerTriggers()
{
    // KRunner asks Jarvis only about queries that start with one of these.
    return {u"jarvis"_s, u"джарвис"_s, u"открой"_s, u"запусти"_s, u"open"_s, u"launch"_s, u"переключись"_s, u"переключи"_s,
            u"switch"_s, u"подготовь"_s, u"prepare"_s, u"найди"_s, u"find"_s, u"покажи"_s, u"show"_s, u"режим"_s, u"профиль"_s,
            u"workspace"_s, u"включи"_s, u"вернись"_s};
}

QString runnerQuestion(const QString &query)
{
    static const QRegularExpression head(u"^\\s*(\\S+?)[,:]?(?:\\s+|$)"_s);
    const auto m = head.match(query);
    static const QStringList names{u"jarvis"_s, u"джарвис"_s};
    if (!m.hasMatch() || !names.contains(m.captured(1).toLower()))
        return {};
    const QString question = query.mid(m.capturedEnd()).simplified();
    if (question.size() < 2 || question.size() > 500)
        return {};
    return question;
}

QString plasmaCountScript()
{
    return hudCollector()
           + u"var n = 0;\n"
             u"for (var i = 0; i < all.length; ++i) n += jarvisWidgets(all[i]).length;\n"
             u"print(n);\n"_s;
}

QString plasmaAddScript(const QString &type)
{
    static const QRegularExpression jarvisWidget(u"^org\\.jarvis\\.(hud|dock|system|network|power|media|status)$"_s);
    if (!jarvisWidget.match(type).hasMatch())
        return {};
    return hudCollector().replace(QString(kHudPlugin), type)
           + u"var n = 0;\n"
             u"for (var i = 0; i < all.length; ++i) n += jarvisWidgets(all[i]).length;\n"
             u"if (n === 0) {\n"
             u"    var d = desktopForScreen(0) || desktops()[0];\n"
             u"    if (d) {\n"
             u"        var g = screenGeometry(d.screen);\n"
             u"        if (d.addWidget(\"%1\", Math.max(0, g.width - 380), 80, 320, 400)) n = 1;\n"
             u"    }\n"
             u"}\n"
             u"print(n);\n"_s.arg(type);
}

QString plasmaRemoveScript(const QString &type)
{
    static const QRegularExpression jarvisWidget(u"^(org\\.jarvis\\.(hud|dock|system|network|power|media|status))?$"_s);
    if (!jarvisWidget.match(type).hasMatch())
        return {};
    // "" = every Jarvis widget: filter the containment's widgets by the id prefix.
    const QString select = type.isEmpty() ? u"c.widgets().filter(function (w) { return String(w.type).indexOf(\"org.jarvis.\") === 0; })"_s
                                          : u"c.widgets(\"%1\")"_s.arg(type);
    return u"var all = desktops().concat(panels());\nfunction jarvisWidgets(c) { return %1; }\n"_s.arg(select)
           + u"var n = 0;\n"
             u"for (var i = 0; i < all.length; ++i) {\n"
             u"    var ws = jarvisWidgets(all[i]);\n"
             u"    for (var j = 0; j < ws.length; ++j) { ws[j].remove(); ++n; }\n"
             u"}\n"
             u"print(n);\n"_s;
}

QJsonObject assessUnit(const UnitHealth &u, bool wanted, Lang lang)
{
    QString level, text;
    if (u.load != u"loaded") {
        level = wanted ? u"error"_s : u"off"_s;
        text = u.load == u"masked" ? pick(lang, u"заблокирована (masked): systemctl --user unmask %1"_s, u"masked: systemctl --user unmask %1"_s).arg(u.unit)
                                   : pick(lang, u"не установлена"_s, u"not installed"_s);
    } else if (u.active == u"active") {
        level = u.restarts > 0 ? u"warn"_s : u"ok"_s;
        text = pick(lang, u"работает · память %1 МБ · ЦП %2 с"_s, u"running · memory %1 MB · CPU %2 s"_s)
                   .arg(u.memoryBytes / (1024 * 1024))
                   .arg(double(u.cpuNs) / 1e9, 0, 'f', 1);
        if (u.restarts > 0)
            text += pick(lang, u" · перезапусков после сбоев: %1"_s, u" · restarted after failures: %1"_s).arg(u.restarts);
    } else if (u.active == u"activating" || u.active == u"reloading" || u.sub == u"auto-restart") {
        level = u"warn"_s;
        text = u.sub == u"auto-restart" ? pick(lang, u"перезапускается после сбоя"_s, u"restarting after a failure"_s)
                                         : pick(lang, u"запускается"_s, u"starting"_s);
    } else if (u.active == u"failed") {
        level = u"error"_s;
        if (u.result == u"start-limit-hit")
            text = pick(lang, u"остановлена после повторных сбоев (лимит перезапусков); устраните причину и нажмите «Перезапустить»"_s,
                        u"stopped after repeated failures (restart limit); fix the cause and press Restart"_s);
        else if (u.result == u"watchdog")
            text = pick(lang, u"зависла и была остановлена сторожевым таймером"_s, u"hung and was stopped by the watchdog"_s);
        else
            text = pick(lang, u"ошибка (%1)"_s, u"failed (%1)"_s).arg(u.result.isEmpty() ? u"?"_s : u.result);
    } else {
        level = wanted ? u"warn"_s : u"off"_s;
        text = pick(lang, u"не запущена"_s, u"not running"_s);
    }
    if (u.load == u"loaded" && !u.fileState.isEmpty())
        text += u" · "_s + u.fileState;
    return QJsonObject{{u"unit"_s, u.unit}, {u"level"_s, level}, {u"text"_s, text}};
}

} // namespace jarvis::integration
