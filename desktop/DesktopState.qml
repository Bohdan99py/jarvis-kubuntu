import QtQuick

// What JarvisCore reads from its `vs` (the window's VisualStateController),
// taken from jarvisd's status on the desktop: the same core, fed by the
// service instead of the window. Real numbers only; no demo on the desktop.
QtObject {
    property var status: ({})
    property bool online: false
    readonly property var activity: status.activity || {}
    readonly property var voice: status.voice || {}
    readonly property string state: !online ? "offline"
        : status.state === "processing" ? "processing"
        : status.state === "error" ? "error"
        : voice.speaking ? "speaking"
        : voice.listening ? "listening"
        : status.state === "notification" ? "notification"
        : "idle"
    readonly property bool demo: false
    readonly property var routes: activity.routes || []
    readonly property var counters: ({ requests: activity.requests || 0, errors: activity.errors || 0 })
    readonly property bool streaming: false
    readonly property real streamRate: 0
    readonly property real lastLatency: activity.latency || 0
}
