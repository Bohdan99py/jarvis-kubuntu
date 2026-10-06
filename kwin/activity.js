// Jarvis activity bridge. KWin runs this script while activity tracking is on
// and reports the focused window (title, application class, desktop file) to
// jarvisd over the session bus. It reads nothing else and sends nothing else.
// Works with Plasma 6 (windowActivated) and Plasma 5 (clientActivated).
var service = "%SERVICE%";
var current = null;

function report(window) {
    var caption = "", appClass = "", desktop = "";
    if (window && !window.desktopWindow && !window.dock && !window.specialWindow) {
        caption = String(window.caption || "");
        appClass = String(window.resourceClass || "");
        desktop = String(window.desktopFileName || "");
    }
    callDBus(service, "/org/jarvis/Daemon1", "org.jarvis.Daemon1", "WindowActivated",
             caption, appClass, desktop);
}

function onCaptionChanged() {
    report(current);
}

function onActivated(window) {
    if (current && current.captionChanged) {
        try { current.captionChanged.disconnect(onCaptionChanged); } catch (e) {}
    }
    current = window;
    if (current && current.captionChanged)
        current.captionChanged.connect(onCaptionChanged);
    report(current);
}

if (workspace.windowActivated)
    workspace.windowActivated.connect(onActivated);
else
    workspace.clientActivated.connect(onActivated);
onActivated(workspace.activeWindow !== undefined ? workspace.activeWindow : workspace.activeClient);
