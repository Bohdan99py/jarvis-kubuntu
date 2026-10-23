import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.kde.plasma.workspace.dbus as DBus
import org.kde.taskmanager as TaskManager

// Jarvis HUD, a Plasma 6 widget. Pure QML on Plasma's own modules: nothing of
// Jarvis is loaded into plasmashell, so a broken or missing Jarvis can only
// leave this widget empty — never the desktop.
//
// It talks to jarvisd over org.jarvis.Integration1 (see desktop_integration.h):
// the state comes as StatusChanged signals, the system figures are asked for
// every few seconds while the widget is seen. It never starts the daemon by
// calling it: calls go out only while org.jarvis.Daemon1 is on the bus.
PlasmoidItem {
    id: root

    readonly property string service: "org.jarvis.Daemon1"
    readonly property string objectPath: "/org/jarvis/Integration1"
    readonly property string iface: "org.jarvis.Integration1"

    property var status: ({})
    property var metrics: ({})
    readonly property bool online: daemon.registered
    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar || Plasmoid.formFactor === PlasmaCore.Types.MediaCenter
    // A maximized or fullscreen window on this screen hides the desktop: the core rests.
    readonly property bool covered: onDesktop && windows.covering
    readonly property bool seen: onDesktop ? !covered : root.expanded
    // The service's choice (the user's intensity, one step lower on battery), off when nobody sees it.
    readonly property string intensity: !online || covered ? "off" : ((status.animation && status.animation.effective) || "normal")

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "org.jarvis.Jarvis"
    preferredRepresentation: onDesktop ? fullRepresentation : compactRepresentation
    toolTipMainText: "J.A.R.V.I.S."
    toolTipSubText: !online ? i18n("Jarvis is not running.") : status.notice ? i18n("Last notification: %1", status.notice.title) : i18n("Jarvis is ready.")

    function parse(json) {
        try { return JSON.parse(json) } catch (e) { return {} }
    }
    function call(member, args, signature, done) {
        if (!root.online)
            return
        DBus.SessionBus.asyncCall({ service: root.service, path: root.objectPath, interface: root.iface,
                                    member: member, arguments: args, signature: signature },
                                  reply => { if (done) done(reply.value) },
                                  reply => console.warn("Jarvis HUD:", member, reply.error.message))
    }
    function refreshStatus() { call("Status", [], "", value => root.status = parse(value)) }
    function refreshMetrics() { call("SystemMetrics", [], "", value => root.metrics = parse(value)) }
    function run(action) { call("RunAction", [action], "s") }
    // Starts the Jarvis tray application through systemd (it brings the service up).
    function startJarvis() {
        DBus.SessionBus.asyncCall({ service: "org.freedesktop.systemd1", path: "/org/freedesktop/systemd1",
                                    interface: "org.freedesktop.systemd1.Manager", member: "StartUnit",
                                    arguments: ["jarvis-tray.service", "replace"], signature: "ss" },
                                  () => {},
                                  reply => console.warn("Jarvis HUD: cannot start jarvis-tray.service:", reply.error.message))
    }

    // The figures come from the timer below, which starts with the service.
    Component.onCompleted: if (daemon.registered) refreshStatus()
    DBus.DBusServiceWatcher {
        id: daemon
        busType: DBus.BusType.Session
        watchedService: root.service
        onRegisteredChanged: {
            if (registered) {
                root.refreshStatus()
            } else {
                root.status = {}
                root.metrics = {}
            }
        }
    }
    DBus.SignalWatcher {
        busType: DBus.BusType.Session
        service: root.service
        path: root.objectPath
        iface: root.iface
        enabled: root.online
        function dbusStatusChanged(json) { root.status = root.parse(json) }
        // The status carries the notification; StatusChanged follows anyway.
        function dbusNoticePosted(json) {}
    }
    // System figures: every 3 s while seen, rarely while covered or folded away.
    Timer {
        interval: root.seen ? 3000 : 20000
        running: root.online && (root.seen || root.onDesktop)
        repeat: true
        triggeredOnStart: true
        onTriggered: root.refreshMetrics()
    }
    // A missed signal (plasmashell or the service restarted) heals within a minute.
    Timer { interval: 60000; running: root.online; repeat: true; onTriggered: root.refreshStatus() }

    TaskManager.VirtualDesktopInfo { id: virtualDesktops }
    TaskManager.ActivityInfo { id: activityInfo }
    TaskManager.TasksModel {
        id: windows
        property bool covering: false
        filterByVirtualDesktop: true
        filterByActivity: true
        filterByScreen: true
        filterMinimized: true
        screenGeometry: Plasmoid.containment ? Plasmoid.containment.screenGeometry : Qt.rect(0, 0, 0, 0)
        virtualDesktop: virtualDesktops.currentDesktop
        activity: activityInfo.currentActivity
        groupMode: TaskManager.TasksModel.GroupDisabled
        function check() {
            let found = false
            for (let i = 0; i < count && !found; ++i) {
                const idx = index(i, 0)
                found = data(idx, TaskManager.AbstractTasksModel.IsMaximized) === true
                     || data(idx, TaskManager.AbstractTasksModel.IsFullScreen) === true
            }
            covering = found
        }
        onCountChanged: Qt.callLater(check)
        onDataChanged: Qt.callLater(check)
        onModelReset: Qt.callLater(check)
    }

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("Open the overlay")
            icon.name: "org.jarvis.Jarvis"
            enabled: root.online && root.status.integration === true
            onTriggered: root.run("overlay")
        },
        PlasmaCore.Action {
            text: root.online ? i18n("Open Jarvis") : i18n("Start Jarvis")
            icon.name: "window"
            onTriggered: root.online ? root.run("main") : root.startJarvis()
        },
        PlasmaCore.Action {
            text: root.status.awareness && root.status.awareness.paused ? i18n("Resume awareness") : i18n("Pause awareness for an hour")
            icon.name: "media-playback-pause"
            visible: root.status.integration === true && !!root.status.awareness && root.status.awareness.level !== "off"
            onTriggered: root.run(root.status.awareness.paused ? "resume-awareness" : "pause-awareness")
        }
    ]

    compactRepresentation: MouseArea {
        Layout.minimumWidth: Kirigami.Units.iconSizes.small
        Layout.minimumHeight: Kirigami.Units.iconSizes.small
        hoverEnabled: true
        onClicked: root.expanded = !root.expanded
        MiniCore {
            anchors.fill: parent
            compact: true
            online: root.online
            hudState: root.status.state || "idle"
            // In a panel the core only stirs: never more than the low intensity.
            intensity: root.intensity === "off" ? "off" : "low"
        }
    }

    fullRepresentation: HudView {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 16
        Layout.preferredHeight: Kirigami.Units.gridUnit * 22
        Layout.minimumWidth: Kirigami.Units.gridUnit * 9
        Layout.minimumHeight: Kirigami.Units.gridUnit * 8
        online: root.online
        status: root.status
        metrics: root.metrics
        intensity: root.intensity
        covered: root.covered
        onOverlayRequested: root.run("overlay")
        onOpenRequested: root.run("main")
        onStartRequested: root.startJarvis()
    }
}
