import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import org.jarvis.visual as JV

// The Jarvis core on the Plasma desktop (0.19.2): the window's own JarvisCore
// (module org.jarvis.visual), fed by jarvisd. Large on an empty desktop, a
// background element when windows are open, still under maximized or
// fullscreen windows; real events (answers, notifications, workspace
// switches) make it pulse. Pure QML on Plasma's own modules: a broken or
// stopped Jarvis leaves only this widget empty — never the desktop.
PlasmoidItem {
    id: root

    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar || Plasmoid.formFactor === PlasmaCore.Types.MediaCenter
    // A maximized or fullscreen window on this screen hides the desktop: the core rests.
    readonly property bool covered: onDesktop && windows.covering
    readonly property bool seen: onDesktop ? !covered : root.expanded
    readonly property bool animate: Plasmoid.configuration.effects && link.desktop.effects !== false
                                    && !(link.status.animation && link.status.animation.effective === "off")

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "org.jarvis.Jarvis"
    preferredRepresentation: onDesktop ? fullRepresentation : compactRepresentation
    toolTipMainText: "J.A.R.V.I.S."
    toolTipSubText: !link.online ? i18nd("jarvis-desktop", "Jarvis is not running.")
                  : link.status.notice ? i18nd("jarvis-desktop", "Last notification: %1", link.status.notice.title)
                  : i18nd("jarvis-desktop", "Jarvis is ready.")

    JV.DaemonLink {
        id: link
        component: "core"
        kinds: Plasmoid.configuration.extra ? ["cpu", "memory"] : []
        interval: 3000
        polling: root.seen
        onEvent: e => root.pulse(e.kind === "error" ? 1.0 : e.kind === "notice" || e.kind === "workspace" ? 0.85 : 0.6)
    }
    signal pulse(real strength)

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
            text: i18nd("jarvis-desktop", "Open the overlay")
            icon.name: "org.jarvis.Jarvis"
            enabled: link.integrated
            onTriggered: link.run("overlay")
        },
        PlasmaCore.Action {
            text: link.online ? i18nd("jarvis-desktop", "Open Jarvis") : i18nd("jarvis-desktop", "Start Jarvis")
            icon.name: "window"
            onTriggered: link.online ? link.run("main") : link.startJarvis()
        }
    ]

    compactRepresentation: MouseArea {
        id: compact
        Layout.minimumWidth: Kirigami.Units.iconSizes.small
        Layout.minimumHeight: Kirigami.Units.iconSizes.small
        hoverEnabled: true
        onClicked: link.integrated ? link.run("overlay") : (root.expanded = !root.expanded)
        JV.DesktopCore {
            id: smallCore
            anchors.fill: parent
            compact: true
            status: link.status
            online: link.online
            quality: "low"
            // In a panel the core moves only while Jarvis works or the pointer is on it.
            active: root.animate && (compact.containsMouse || smallCore.state !== "idle")
        }
    }

    fullRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 24
        Layout.preferredHeight: Kirigami.Units.gridUnit * 26
        Layout.minimumWidth: Kirigami.Units.gridUnit * 6
        Layout.minimumHeight: Kirigami.Units.gridUnit * 6

        // The shade under the core: readable on any wallpaper, no visible frame.
        JV.CoreBackdrop {
            anchors.centerIn: core
            width: core.width; height: core.height
            strength: Plasmoid.configuration.backdrop / 100
            soft: Plasmoid.configuration.backdropSoft
        }
        JV.DesktopCore {
            id: core
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, parent.height - (caption.visible ? caption.height : 0))
            height: width
            status: link.status
            online: link.online
            quality: link.desktop.quality || "balanced"
            active: root.seen && root.animate
            metrics: Plasmoid.configuration.extra && link.telemetry.cpu
                     ? { cpu: link.telemetry.cpu.usage, memory: link.telemetry.memory ? link.telemetry.memory.percent : -1 } : {}
            onActivated: link.integrated ? link.run("overlay") : link.online ? link.run("main") : link.startJarvis()
            Connections { target: root; function onPulse(strength) { core.pulse(strength) } }
        }
        Rectangle {
            visible: caption.visible && JV.JarvisTheme.lightBackdrop
            anchors.centerIn: caption
            width: caption.width + 24; height: caption.height + 10
            radius: height / 2
            color: JV.JarvisTheme.alpha(JV.JarvisTheme.surface, 0.8)
        }
        Column {
            id: caption
            visible: Plasmoid.configuration.expanded && parent.height > Kirigami.Units.gridUnit * 10
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 2
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: !link.online ? i18nd("jarvis-desktop", "OFFLINE · click to start Jarvis")
                    : link.outdated ? i18nd("jarvis-desktop", "Restart the Jarvis service after the update")
                    : JV.JarvisTheme.stateName(core.state).toUpperCase()
                      + (link.status.workspace ? "  ·  " + (link.status.workspace.current || "").toUpperCase() : "")
                color: JV.JarvisTheme.stateColor(core.state)
                font.family: JV.JarvisTheme.mono
                font.pixelSize: 12
                font.letterSpacing: 3
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: link.integrated && !!link.status.notice
                text: link.status.notice ? link.status.notice.title : ""
                color: JV.JarvisTheme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
                width: Math.min(implicitWidth, root.width - 20)
                textFormat: Text.PlainText
            }
        }
    }
}
