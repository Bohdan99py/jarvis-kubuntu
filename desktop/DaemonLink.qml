import QtQuick
import org.kde.plasma.workspace.dbus as DBus

// jarvisd for a desktop widget (Plasma only): status, telemetry, events and
// actions over org.jarvis.Integration1. Every widget has one; jarvisd caches
// the measurements, so several widgets cost what one does. Calls go out only
// while org.jarvis.Daemon1 is on the bus — a widget never starts the service
// by asking it something — and the theme tokens follow the user's settings.
Item {
    id: link
    property string component: ""   // heartbeat for the desktop shell's check: core, dock, system…
    property var kinds: []          // telemetry this widget shows: cpu, memory, gpu, thermal, net, power, media
    property int interval: 3000     // ms between measurements while `polling`
    property bool polling: true
    property var status: ({})
    property var telemetry: ({})
    readonly property bool online: watcher.registered
    readonly property bool integrated: online && status.integration === true
    readonly property bool outdated: online && status.version !== undefined && status.workspace === undefined
    readonly property var desktop: status.desktop || {}
    signal event(var e)

    visible: false

    function parse(json) {
        try { return JSON.parse(json) } catch (e) { return {} }
    }
    // Plasma's D-Bus module encodes the arguments by a tuple signature: "(s)", "(ss)".
    function call(member, args, signature, done) {
        if (!link.online)
            return
        DBus.SessionBus.asyncCall({ service: "org.jarvis.Daemon1", path: "/org/jarvis/Integration1", interface: "org.jarvis.Integration1",
                                    member: member, arguments: args, signature: signature },
                                  reply => { if (done) done(reply.value) },
                                  reply => console.warn("Jarvis:", member, reply.error.message))
    }
    function refreshStatus() { call("Status", [], "", value => link.status = parse(value)) }
    function refreshTelemetry() {
        if (link.kinds.length > 0)
            call("Telemetry", [link.kinds.join(",")], "(s)", value => link.telemetry = parse(value))
    }
    function run(action) { call("RunAction", [action], "(s)") }
    function media(action) { call("MediaControl", [action], "(s)", () => refreshTelemetry()) }
    function workspace(id) { call("Workspace", [id], "(s)") }
    // The Jarvis tray application through systemd; it brings the service up.
    function startJarvis() {
        DBus.SessionBus.asyncCall({ service: "org.freedesktop.systemd1", path: "/org/freedesktop/systemd1",
                                    interface: "org.freedesktop.systemd1.Manager", member: "StartUnit",
                                    arguments: ["jarvis-tray.service", "replace"], signature: "(ss)" },
                                  () => {}, reply => console.warn("Jarvis: cannot start jarvis-tray.service:", reply.error.message))
    }
    function applyTheme() {
        const d = link.status ? link.status.desktop || {} : {}
        if (!d.theme)
            return
        JarvisTheme.preset = d.theme
        JarvisTheme.customPrimary = d.primary ? d.primary : "transparent"
        JarvisTheme.customSecondary = d.secondary ? d.secondary : "transparent"
        JarvisTheme.glow = d.glow
        JarvisTheme.glassOpacity = d.opacity
        JarvisTheme.motion = d.motion
        JarvisTheme.density = d.density
    }
    onStatusChanged: applyTheme()

    DBus.DBusServiceWatcher {
        id: watcher
        busType: DBus.BusType.Session
        watchedService: "org.jarvis.Daemon1"
        onRegisteredChanged: {
            if (registered) {
                link.refreshStatus()
                link.refreshTelemetry()
                if (link.component) link.call("ComponentAlive", [link.component], "(s)")
            } else {
                link.status = {}
                link.telemetry = {}
            }
        }
    }
    DBus.SignalWatcher {
        busType: DBus.BusType.Session
        service: "org.jarvis.Daemon1"
        path: "/org/jarvis/Integration1"
        iface: "org.jarvis.Integration1"
        enabled: link.online
        function dbusStatusChanged(json) { link.status = link.parse(json) }
        function dbusEventPosted(json) { link.event(link.parse(json)) }
        function dbusNoticePosted(json) { const n = link.parse(json); n.kind = "notice"; link.event(n) }
    }
    Component.onCompleted: if (watcher.registered) { refreshStatus(); refreshTelemetry(); if (component) call("ComponentAlive", [component], "(s)") }
    Timer {
        interval: link.interval
        running: link.online && link.polling && link.kinds.length > 0
        repeat: true
        onTriggered: link.refreshTelemetry()
    }
    // A missed signal (plasmashell or the service restarted) heals within a minute; the heartbeat rides along.
    Timer {
        interval: 60000
        running: link.online
        repeat: true
        onTriggered: {
            link.refreshStatus()
            if (link.component) link.call("ComponentAlive", [link.component], "(s)")
        }
    }
}
