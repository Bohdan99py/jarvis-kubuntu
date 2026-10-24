// Jarvis workspace script. jarvisd loads it once for a workspace switch (or to
// save a layout), KWin runs it, it reports back and jarvisd unloads it again.
// It touches only windows of the applications the profile names: moves them to
// the profile's virtual desktop and puts them where the user saved them, then
// switches to that desktop. It reads nothing but the application id and the
// geometry of those windows. Plasma 6 (windowList, desktops) and Plasma 5
// (clientList) both work.
var data = %DATA%;
var service = "%SERVICE%";

function appId(w) {
    return String(w.desktopFileName || w.resourceClass || "");
}

var windows = workspace.windowList ? workspace.windowList() : workspace.clientList();
var desktops = workspace.desktops || [];
var target = data.desktop >= 1 && data.desktop <= desktops.length ? desktops[data.desktop - 1] : null;
var running = [];
var geometries = {};
for (var i = 0; i < windows.length; ++i) {
    var w = windows[i];
    if (!w.normalWindow || w.skipTaskbar)
        continue;
    var id = appId(w);
    if (!id)
        continue;
    if (running.indexOf(id) < 0)
        running.push(id);
    if (data.apps.indexOf(id) < 0)
        continue;
    if (data.mode === "save") {
        var g = w.frameGeometry;
        geometries[id] = { x: Math.round(g.x), y: Math.round(g.y), width: Math.round(g.width), height: Math.round(g.height) };
        continue;
    }
    if (target && w.desktops !== undefined)
        w.desktops = [target];
    var saved = data.layout[id];
    if (saved && !w.fullScreen && saved.width > 50 && saved.height > 50) {
        var r = w.frameGeometry;
        r.x = saved.x; r.y = saved.y; r.width = saved.width; r.height = saved.height;
        w.frameGeometry = r;
    }
}
if (data.mode === "switch" && target)
    workspace.currentDesktop = target;
callDBus(service, "/org/jarvis/Integration1", "org.jarvis.Integration1", "WorkspaceReport",
         JSON.stringify({ token: data.token, mode: data.mode, running: running, geometries: geometries,
                          desktops: desktops.length, switched: data.mode === "switch" && !!target }));
