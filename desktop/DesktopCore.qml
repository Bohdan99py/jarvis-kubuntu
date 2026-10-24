import QtQuick

// The Jarvis core on the desktop: the window's JarvisCore and HudController
// (the same files), driven by jarvisd's status. Quality as in the window —
// low, balanced, cinematic — with a desktop's manners: while Jarvis is idle
// the clock runs at a few frames a second, and it stops altogether when
// nobody sees it (`active` false: covered by a maximized or fullscreen
// window, or the effects are off). Real events make it pulse.
Item {
    id: root
    property var status: ({})
    property bool online: false
    property var metrics: ({})        // {cpu, memory} percent for the gauge arcs
    property string quality: "balanced"
    property bool active: true
    property bool compact: false
    readonly property alias state: vs.state
    signal activated()

    // A real event (an answer, a notification, a workspace switch).
    function pulse(strength) { if (ctl.running) ctl.fire(strength) }

    DesktopState { id: vs; status: root.status; online: root.online }
    HudController {
        id: ctl
        running: root.active && root.visible && root.width > 0
        quality: JarvisTheme.minimal ? "low" : root.quality
        hudState: vs.state
        // Idle on the desktop: 6, 12 or 24 frames a second; any activity gets the full rate.
        maxRate: vs.state === "idle" || vs.state === "offline"
                 ? (quality === "low" ? 6 : quality === "cinematic" ? 24 : 12) : 60
    }
    JarvisCore {
        anchors.fill: parent
        ctl: ctl
        vs: vs
        compact: root.compact
        cpu: root.metrics.cpu !== undefined ? root.metrics.cpu : -1
        ram: root.metrics.memory !== undefined ? root.metrics.memory : -1
        onActivated: root.activated()
    }
}
