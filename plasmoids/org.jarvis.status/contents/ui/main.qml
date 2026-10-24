import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.jarvis.visual as JV

// Jarvis Status (0.19.2): Plasma's side of the widget — the link to jarvisd, the
// compact and expanded views, the workspace that shows or hides it. What it
// draws is StatusView.qml, plain Qt Quick (checked outside Plasma by the tests).
PlasmoidItem {
    id: root
    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar || Plasmoid.formFactor === PlasmaCore.Types.MediaCenter
    readonly property var workspace: link.status.workspace || {}
    // In JARVIS DESKTOP the current workspace decides which widgets are shown.
    readonly property bool inProfile: {
        if (link.desktop.mode !== "jarvis" || !workspace.profiles)
            return true
        const p = workspace.profiles.find(x => x.id === workspace.current)
        return !p || p.widgets.indexOf(Plasmoid.pluginName) >= 0
    }
    readonly property bool shown: inProfile && (onDesktop || root.expanded)
    readonly property bool effects: Plasmoid.configuration.effects && link.desktop.effects !== false

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.icon: "org.jarvis.Jarvis"
    preferredRepresentation: onDesktop && Plasmoid.configuration.expanded ? fullRepresentation : compactRepresentation
    toolTipMainText: "J.A.R.V.I.S."

    JV.DaemonLink {
        id: link
        component: "status"
        kinds: []
        // Low quality: half as often; nobody looking: rarely.
        interval: !root.shown ? 15000 : (link.desktop.quality === "low" ? 10000 : 10000)
        polling: root.inProfile
    }

    compactRepresentation: MouseArea {
        Layout.minimumWidth: chips.implicitWidth + 8
        Layout.minimumHeight: Kirigami.Units.iconSizes.small
        onClicked: root.expanded = !root.expanded
        StatusViewCompact {
            id: chips
            anchors.centerIn: parent
            telemetry: link.telemetry
            status: link.status
            online: link.online
        }
    }

    fullRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 18
        Layout.preferredHeight: Kirigami.Units.gridUnit * 22
        Layout.minimumWidth: Kirigami.Units.gridUnit * 10
        Layout.minimumHeight: Kirigami.Units.gridUnit * 6
        opacity: root.inProfile ? 1 : 0
        enabled: root.inProfile
        Behavior on opacity { NumberAnimation { duration: 300 } }
        StatusView {
            anchors.fill: parent
            telemetry: link.telemetry
            status: link.status
            online: link.online
            effects: root.effects
            extra: Plasmoid.configuration.extra
            onAction: (name, arg) => name === "start" ? link.startJarvis() : link.run(name)
        }
    }
}
