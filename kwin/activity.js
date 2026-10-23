// Jarvis activity bridge. KWin runs this script while activity tracking or
// window awareness is on and reports the focused window — title, application
// class, desktop file, KWin's window id (for a screenshot of exactly that
// window, when the user allows screen awareness), whether it is fullscreen and
// which screen it is on — to jarvisd over the session bus. It reads nothing
// else and sends nothing else. Works with Plasma 6 (windowActivated) and
// Plasma 5 (clientActivated).
var service = "%SERVICE%";
var current = null;

function report(window) {
    var caption = "", appClass = "", desktop = "", handle = "", output = "", fullscreen = false;
    if (window && !window.desktopWindow && !window.dock && !window.specialWindow) {
        caption = String(window.caption || "");
        appClass = String(window.resourceClass || "");
        desktop = String(window.desktopFileName || "");
        handle = window.internalId !== undefined ? String(window.internalId) : "";
        fullscreen = !!window.fullScreen;
        var screen = window.output !== undefined ? window.output : window.screen;
        output = screen && screen.name !== undefined ? String(screen.name) : "";
    }
    callDBus(service, "/org/jarvis/Daemon1", "org.jarvis.Daemon1", "WindowFocused",
             caption, appClass, desktop, handle, fullscreen, output);
}

function onChanged() {
    report(current);
}

function onActivated(window) {
    if (current) {
        try { current.captionChanged.disconnect(onChanged); } catch (e) {}
        try { current.fullScreenChanged.disconnect(onChanged); } catch (e) {}
    }
    current = window;
    if (current) {
        try { current.captionChanged.connect(onChanged); } catch (e) {}
        try { current.fullScreenChanged.connect(onChanged); } catch (e) {}
    }
    report(current);
}

if (workspace.windowActivated)
    workspace.windowActivated.connect(onActivated);
else
    workspace.clientActivated.connect(onActivated);
onActivated(workspace.activeWindow !== undefined ? workspace.activeWindow : workspace.activeClient);
