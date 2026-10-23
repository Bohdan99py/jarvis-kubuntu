import QtQuick
import org.kde.layershell as LayerShell
import Jarvis

// The overlay as a Wayland layer-shell surface (KDE's layer-shell-qt): above
// every window, even fullscreen ones when the user asked for it, on the screen
// that is active, taking the keyboard while it is open. Loaded at run time only
// when the module is installed and the session is Wayland; otherwise Jarvis
// uses HudOverlay as a plain always-on-top window.
HudOverlay {
    layer: true
    LayerShell.Window.scope: "jarvis-overlay"
    LayerShell.Window.layer: open ? LayerShell.Window.LayerOverlay : LayerShell.Window.LayerTop
    LayerShell.Window.anchors: open ? LayerShell.Window.AnchorTop : (LayerShell.Window.AnchorTop | LayerShell.Window.AnchorRight)
    LayerShell.Window.keyboardInteractivity: open ? LayerShell.Window.KeyboardInteractivityOnDemand : LayerShell.Window.KeyboardInteractivityNone
    LayerShell.Window.wantsToBeOnActiveScreen: true
    LayerShell.Window.activateOnShow: true
}
