#include "desktop_shell_scripts.h"

#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace jarvis::shell {
namespace {

QString json(const QJsonValue &v)
{
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return u"null"_s;
}

QJsonObject rect(int x, int y, int w, int h)
{
    return QJsonObject{{u"x"_s, x}, {u"y"_s, y}, {u"width"_s, w}, {u"height"_s, h}};
}

// Helpers every script shares: walk desktops and panels.
const char16_t *kPrelude = uR"JS(
function jarvisAll() { return desktops().concat(panels()); }
function jarvisFindWidget(id) {
    var all = jarvisAll();
    for (var i = 0; i < all.length; ++i) { var w = all[i].widgetById(id); if (w) return w; }
    return null;
}
function jarvisFindPanel(id) {
    var ps = panels();
    for (var i = 0; i < ps.length; ++i) if (ps[i].id === id) return ps[i];
    return null;
}
)JS";

} // namespace

QStringList widgetIds()
{
    return {u"org.jarvis.hud"_s, u"org.jarvis.dock"_s, u"org.jarvis.system"_s, u"org.jarvis.network"_s,
            u"org.jarvis.power"_s, u"org.jarvis.media"_s, u"org.jarvis.status"_s};
}

QString inspectScript()
{
    return QString::fromUtf16(kPrelude) + uR"JS(
var out = { version: String(applicationVersion), screens: screenCount, panels: [], desktops: [], launchers: [], locked: false };
var ps = panels();
for (var i = 0; i < ps.length; ++i) {
    var p = ps[i], types = [], ws = p.widgets();
    for (var j = 0; j < ws.length; ++j) {
        types.push(ws[j].type);
        if ((ws[j].type === "org.kde.plasma.icontasks" || ws[j].type === "org.kde.plasma.taskmanager") && out.launchers.length === 0) {
            ws[j].currentConfigGroup = ["General"];
            var l = ws[j].readConfig("launchers");
            if (l) out.launchers = Array.isArray(l) ? l : String(l).split(",");
        }
    }
    out.panels.push({ id: p.id, location: String(p.location), screen: p.screen, height: p.height, widgets: types,
                      jarvis: types.indexOf("org.jarvis.dock") >= 0 });
}
var ds = desktops();
for (var k = 0; k < ds.length; ++k) {
    var d = ds[k], list = [], dw = d.widgets();
    for (var m = 0; m < dw.length; ++m) list.push({ id: dw[m].id, type: dw[m].type });
    out.desktops.push({ id: d.id, screen: d.screen, widgets: list });
    if (d.locked) out.locked = true;
}
var g = screenGeometry(0);
out.screen = { width: Math.round(g.width), height: Math.round(g.height) };
print(JSON.stringify(out));
)JS"_s;
}

QJsonObject makePlan(const QJsonObject &inspection, const QJsonObject &desktop, const QStringList &workspaceWidgets)
{
    const QJsonObject screen = inspection.value(u"screen"_s).toObject();
    const int w = std::max(800, screen.value(u"width"_s).toInt(1920)), h = std::max(600, screen.value(u"height"_s).toInt(1080));
    // The core: a square, sized and placed as the user chose.
    const QString sizeName = desktop.value(u"coreSize"_s).toString();
    int size = sizeName == u"small" ? 320 : sizeName == u"large" ? 640 : 460;
    size = std::min(size, int(std::min(w, h) * 0.8));
    const QString position = desktop.value(u"corePosition"_s).toString();
    const int coreX = position == u"left" ? 80 : position == u"right" ? w - size - 80 : (w - size) / 2;
    const int coreY = std::max(40, (h - size) / 2 - 30);
    // The holographic widgets in two columns that keep clear of the core.
    // The left column starts below the desktop's own icons (Plasma's folder view, top left).
    const int colW = 360, gap = 20, top = 60, leftTop = 140;
    int leftX = 40, rightX = w - colW - 40;
    if (position == u"left")
        leftX = w - 2 * colW - 60;
    else if (position == u"right")
        rightX = 40 + colW + gap;
    QJsonArray widgets;
    int ly = position == u"left" ? top : leftTop, ry = top;
    const auto place = [&](const QString &type, bool left, int height) {
        int &y = left ? ly : ry;
        widgets.append(QJsonObject{{u"type"_s, type}, {u"x"_s, left ? leftX : rightX}, {u"y"_s, y}, {u"width"_s, colW}, {u"height"_s, height}});
        y += height + gap;
    };
    place(u"org.jarvis.status"_s, true, 360);
    place(u"org.jarvis.media"_s, true, 180);
    place(u"org.jarvis.power"_s, true, 160);
    place(u"org.jarvis.system"_s, false, 520);
    place(u"org.jarvis.network"_s, false, 200);

    QStringList launchers;
    for (const QJsonValue &v : inspection.value(u"launchers"_s).toArray())
        // An application's .desktop file only: a pinned executable (an old snap launcher,
        // file:///snap/…/code) shows a blank icon in the dock and starts nothing (0.19.2).
        if (v.toString().startsWith(u"applications:"_s) || (v.toString().startsWith(u"file:"_s) && v.toString().endsWith(u".desktop"_s)))
            launchers.append(v.toString());
    const QJsonArray panels = inspection.value(u"panels"_s).toArray();
    const bool haveDock = std::any_of(panels.begin(), panels.end(), [](const QJsonValue &p) { return p.toObject().value(u"jarvis"_s).toBool(); });
    QJsonArray replace;
    if (desktop.value(u"replacePanels"_s).toBool(true))
        for (const QJsonValue &v : panels)
            if (!v.toObject().value(u"jarvis"_s).toBool())
                replace.append(v.toObject().value(u"id"_s));
    QJsonObject plan{
        {u"screen"_s, QJsonObject{{u"width"_s, w}, {u"height"_s, h}}},
        {u"core"_s, rect(coreX, coreY, size, size)},
        {u"widgets"_s, widgets},
        {u"workspaceWidgets"_s, QJsonArray::fromStringList(workspaceWidgets)},
        {u"dock"_s, QJsonObject{{u"location"_s, desktop.value(u"dockPosition"_s).toString(u"bottom"_s)},
                                {u"height"_s, desktop.value(u"dockSize"_s).toInt(44)},
                                {u"alignment"_s, desktop.value(u"dockAlignment"_s).toString(u"center"_s)},
                                {u"hiding"_s, desktop.value(u"dockAutohide"_s).toBool() ? u"autohide"_s : u"none"_s},
                                {u"launchers"_s, QJsonArray::fromStringList(launchers)},
                                {u"filterByScreen"_s, false}}},
        {u"replacePanels"_s, replace},
        {u"existing"_s, QJsonObject{{u"dock"_s, haveDock}}}};
    return plan;
}

QStringList describePlan(const QJsonObject &plan, bool ru)
{
    QStringList out;
    const QJsonObject core = plan.value(u"core"_s).toObject();
    const QJsonObject dock = plan.value(u"dock"_s).toObject();
    out << (ru ? u"Добавить ядро Jarvis на рабочий стол первого экрана (%1×%1 px)."_s : u"Add the Jarvis core to the desktop of the first screen (%1×%1 px)."_s)
                  .arg(core.value(u"width"_s).toInt());
    out << (ru ? u"Добавить голографические виджеты: система, сеть, питание, медиа, состояние (их видимость зависит от рабочего пространства)."_s
               : u"Add the holographic widgets: system, network, power, media, status (the workspace decides which are shown)."_s);
    if (!plan.value(u"existing"_s).toObject().value(u"dock"_s).toBool())
        out << (ru ? u"Создать панель Jarvis Dock (%1, высота %2 px%3) с системным треем и часами Plasma%4."_s
                   : u"Create a Jarvis Dock panel (%1, %2 px high%3) with Plasma's system tray and clock%4."_s)
                   .arg(dock.value(u"location"_s).toString())
                   .arg(dock.value(u"height"_s).toInt())
                   .arg(dock.value(u"hiding"_s).toString() == u"autohide" ? (ru ? u", автоскрытие"_s : u", auto-hide"_s) : QString())
                   .arg(dock.value(u"launchers"_s).toArray().isEmpty() ? QString()
                                                                       : (ru ? u"; закреплённые программы переносятся"_s : u"; your pinned applications move over"_s));
    const int panels = plan.value(u"replacePanels"_s).toArray().size();
    if (panels > 0)
        out << (ru ? u"После проверки, что док и ядро работают, убрать стандартные панели (%1). Они вернутся при восстановлении."_s
                   : u"Once the dock and the core are verified to work, remove the standard panels (%1). A restore brings them back."_s)
                   .arg(panels);
    else
        out << (ru ? u"Стандартные панели остаются."_s : u"The standard panels stay."_s);
    out << (ru ? u"Перед изменениями сохраняется резервная копия раскладки Plasma; KWin, обои, темы и другие настройки KDE не меняются."_s
               : u"Plasma's layout is backed up first; KWin, the wallpaper, themes and other KDE settings are not changed."_s);
    return out;
}

QString applyScript(const QJsonObject &plan)
{
    return QString::fromUtf16(kPrelude) + u"var plan = %1;\n"_s.arg(json(plan)) + uR"JS(
var created = { panels: [], widgets: [], core: -1, dock: -1 };
var d = desktopForScreen(0) || desktops()[0];
var have = {};
var dw = d.widgets();
for (var i = 0; i < dw.length; ++i) have[dw[i].type] = dw[i].id;
// Only what is not there yet: running it twice adds nothing.
if (have["org.jarvis.hud"] === undefined) {
    var c = d.addWidget("org.jarvis.hud", plan.core.x, plan.core.y, plan.core.width, plan.core.height);
    if (c) { created.widgets.push(c.id); created.core = c.id; }
} else {
    created.core = have["org.jarvis.hud"];
}
for (var k = 0; k < plan.widgets.length; ++k) {
    var s = plan.widgets[k];
    if (have[s.type] !== undefined) continue;
    var w = d.addWidget(s.type, s.x, s.y, s.width, s.height);
    if (w) created.widgets.push(w.id);
}
if (!plan.existing.dock) {
    var p = new Panel("org.kde.panel");
    p.location = plan.dock.location;
    p.height = plan.dock.height;
    p.floating = true;
    p.lengthMode = "fit";
    p.alignment = plan.dock.alignment || "center";
    p.hiding = plan.dock.hiding;
    var dock = p.addWidget("org.jarvis.dock");
    if (dock) {
        dock.currentConfigGroup = ["General"];
        if (plan.dock.launchers.length > 0) dock.writeConfig("launchers", plan.dock.launchers);
        dock.writeConfig("filterByScreen", plan.dock.filterByScreen);
        created.dock = dock.id;
    }
    p.addWidget("org.kde.plasma.systemtray");
    p.addWidget("org.kde.plasma.digitalclock");
    created.panels.push(p.id);
}
print(JSON.stringify(created));
)JS"_s;
}

QString verifyScript(const QJsonObject &created)
{
    return QString::fromUtf16(kPrelude) + u"var created = %1;\n"_s.arg(json(created)) + uR"JS(
var missing = [];
for (var i = 0; i < created.widgets.length; ++i)
    if (!jarvisFindWidget(created.widgets[i])) missing.push(created.widgets[i]);
for (var j = 0; j < created.panels.length; ++j) {
    var p = jarvisFindPanel(created.panels[j]);
    if (!p) { missing.push(created.panels[j]); continue; }
    var types = p.widgets().map(function (w) { return w.type; });
    if (types.indexOf("org.jarvis.dock") < 0) missing.push("dock@" + created.panels[j]);
}
print(JSON.stringify({ ok: missing.length === 0, missing: missing }));
)JS"_s;
}

QString finalizeScript(const QJsonArray &panelIds)
{
    return QString::fromUtf16(kPrelude) + u"var ids = %1;\n"_s.arg(json(panelIds)) + uR"JS(
var removed = 0;
var ps = panels();
for (var i = 0; i < ps.length; ++i) {
    // Never a panel with the Jarvis dock, whatever the plan says.
    var types = ps[i].widgets().map(function (w) { return w.type; });
    if (ids.indexOf(ps[i].id) >= 0 && types.indexOf("org.jarvis.dock") < 0) { ps[i].remove(); ++removed; }
}
print(JSON.stringify({ removed: removed }));
)JS"_s;
}

QString removeScript(const QJsonObject &created)
{
    return QString::fromUtf16(kPrelude) + u"var created = %1;\n"_s.arg(json(created)) + uR"JS(
var removed = 0;
var ps = panels();
for (var i = 0; i < ps.length; ++i) {
    var types = ps[i].widgets().map(function (w) { return w.type; });
    if ((created.panels || []).indexOf(ps[i].id) >= 0 || types.indexOf("org.jarvis.dock") >= 0) { ps[i].remove(); ++removed; }
}
var all = jarvisAll();
for (var j = 0; j < all.length; ++j) {
    var ws = all[j].widgets();
    for (var k = 0; k < ws.length; ++k)
        if (String(ws[k].type).indexOf("org.jarvis.") === 0) { ws[k].remove(); ++removed; }
}
print(JSON.stringify({ removed: removed }));
)JS"_s;
}

QString updateScript(const QJsonObject &created, const QJsonObject &plan)
{
    return QString::fromUtf16(kPrelude) + u"var created = %1;\nvar plan = %2;\n"_s.arg(json(created), json(plan)) + uR"JS(
var changed = 0;
for (var i = 0; i < (created.panels || []).length; ++i) {
    var p = jarvisFindPanel(created.panels[i]);
    if (!p) continue;
    p.location = plan.dock.location;
    p.height = plan.dock.height;
    p.hiding = plan.dock.hiding;
    p.alignment = plan.dock.alignment || "center";
    ++changed;
}
var core = created.core >= 0 ? jarvisFindWidget(created.core) : null;
if (core) {
    var g = core.geometry;
    g.x = plan.core.x; g.y = plan.core.y; g.width = plan.core.width; g.height = plan.core.height;
    core.geometry = g;
    ++changed;
}
print(JSON.stringify({ changed: changed }));
)JS"_s;
}

QJsonObject dockConfigKeys()
{
    // The same defaults and limits as plasmoids/org.jarvis.dock/contents/config/main.xml.
    const auto number = [](int def, int lo, int hi) { return QJsonObject{{u"default"_s, def}, {u"min"_s, lo}, {u"max"_s, hi}}; };
    const auto flag = [](bool def) { return QJsonObject{{u"default"_s, def}}; };
    return QJsonObject{{u"iconSize"_s, number(62, 40, 85)},
                       {u"magnification"_s, flag(true)},
                       {u"magnifyAmount"_s, number(35, 5, 60)},
                       {u"panelStyle"_s, flag(true)},
                       {u"showProfiles"_s, flag(true)},
                       {u"showDesktops"_s, flag(true)},
                       {u"showJarvis"_s, flag(true)},
                       {u"groupWindows"_s, flag(true)}};
}

QString dockScript(const QJsonObject &changes)
{
    QJsonArray keys;
    for (const QString &k : dockConfigKeys().keys())
        keys.append(k);
    return QString::fromUtf16(kPrelude) + u"var changes = %1;\nvar keys = %2;\n"_s.arg(json(changes), json(QJsonObject{{u"k"_s, keys}})) + uR"JS(
var out = { found: false };
var ps = panels();
for (var i = 0; i < ps.length && !out.found; ++i) {
    var p = ps[i], ws = p.widgets();
    for (var j = 0; j < ws.length; ++j) {
        if (ws[j].type !== "org.jarvis.dock") continue;
        var w = ws[j], pc = changes.panel || {}, cc = changes.config || {};
        if (pc.location !== undefined) p.location = pc.location;
        if (pc.height !== undefined) p.height = pc.height;
        if (pc.alignment !== undefined) p.alignment = pc.alignment;
        if (pc.hiding !== undefined) p.hiding = pc.hiding;
        w.currentConfigGroup = ["General"];
        var wrote = false;
        for (var c in cc) { w.writeConfig(c, cc[c]); wrote = true; }
        if (wrote) w.reloadConfig();
        out.found = true;
        out.panel = p.id;
        out.location = String(p.location);
        out.height = p.height;
        out.alignment = String(p.alignment);
        out.hiding = String(p.hiding);
        out.config = {};
        for (var k = 0; k < keys.k.length; ++k) {
            var v = w.readConfig(keys.k[k]);
            if (v !== undefined && v !== null && String(v) !== "") out.config[keys.k[k]] = String(v);
        }
        break;
    }
}
print(JSON.stringify(out));
)JS"_s;
}

QJsonObject parsePrinted(const QString &output)
{
    const qsizetype start = output.indexOf(u'{');
    const qsizetype end = output.lastIndexOf(u'}');
    if (start < 0 || end <= start)
        return {};
    return QJsonDocument::fromJson(output.mid(start, end - start + 1).toUtf8()).object();
}

} // namespace jarvis::shell
