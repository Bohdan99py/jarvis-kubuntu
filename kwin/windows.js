// Jarvis window script (0.22). jarvisd loads it for one request of the agent
// — list the windows, bring one forward, or ask one to close — KWin runs it,
// it reports back and jarvisd unloads it again. It reads only the
// application id, the caption and which window is active; "close" asks the
// window to close exactly as its close button does (the application may ask
// to save), it never kills anything; "maximize" (0.22.1) restores, activates
// and maximizes one. Plasma 6 (windowList, internalId).
var data = %DATA%;
var service = "%SERVICE%";

function appId(w) {
    return String(w.desktopFileName || w.resourceClass || "");
}

// Whether the window fills its work area (KWin's scripting API has no maximize state to read).
function isMaximized(w) {
    try {
        var area = workspace.clientArea(KWin.MaximizeArea, w);
        var g = w.frameGeometry;
        return g.width >= area.width - 2 && g.height >= area.height - 2;
    } catch (e) {
        return false;
    }
}

var windows = workspace.windowList ? workspace.windowList() : workspace.clientList();
var list = [];
var done = false;
for (var i = 0; i < windows.length; ++i) {
    var w = windows[i];
    if (!w.normalWindow || w.skipTaskbar)
        continue;
    var id = String(w.internalId);
    if (data.mode === "activate" && id === data.id) {
        if (w.minimized)
            w.minimized = false;
        workspace.activeWindow = w;
        done = true;
    } else if (data.mode === "close" && id === data.id) {
        w.closeWindow();
        done = true;
    } else if (data.mode === "maximize" && id === data.id) {
        if (w.minimized)
            w.minimized = false;
        workspace.activeWindow = w;
        if (w.maximizable && w.setMaximize)
            w.setMaximize(true, true);
        done = true;
    }
    list.push({ id: id, app: appId(w), caption: String(w.caption || "").substring(0, 120),
                active: w === workspace.activeWindow, maximized: isMaximized(w) });
}
callDBus(service, "/org/jarvis/Integration1", "org.jarvis.Integration1", "WorkspaceReport",
         JSON.stringify({ token: data.token, mode: "windows:" + data.mode, done: done, windows: list }));
