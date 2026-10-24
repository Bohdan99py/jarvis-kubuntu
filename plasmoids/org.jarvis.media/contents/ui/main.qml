import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.jarvis.visual as JV

// Jarvis Media (0.19.2): Plasma's side of the widget — the link to jarvisd, the
// compact and expanded views, the workspace that shows or hides it. What it
// draws is MediaView.qml, plain Qt Quick (checked outside Plasma by the tests).
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
    Plasmoid.icon: "multimedia-player"
    preferredRepresentation: onDesktop && Plasmoid.configuration.expanded ? fullRepresentation : compactRepresentation
    toolTipMainText: "MEDIA"

    JV.DaemonLink {
        id: link
        component: "media"
        kinds: ["media"]
        // Low quality: half as often; nobody looking: rarely.
        interval: !root.shown ? 15000 : (link.desktop.quality === "low" ? 4000 : 2000)
        polling: root.inProfile
    }

    compactRepresentation: MouseArea {
        Layout.minimumWidth: chips.implicitWidth + 8
        Layout.minimumHeight: Kirigami.Units.iconSizes.small
        onClicked: root.expanded = !root.expanded
        MediaViewCompact {
            id: chips
            anchors.centerIn: parent
            telemetry: link.telemetry
            status: link.status
            online: link.online
        }
    }

    fullRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 18
        Layout.preferredHeight: Kirigami.Units.gridUnit * 12
        Layout.minimumWidth: Kirigami.Units.gridUnit * 10
        Layout.minimumHeight: Kirigami.Units.gridUnit * 6
        opacity: root.inProfile ? 1 : 0
        enabled: root.inProfile
        Behavior on opacity { NumberAnimation { duration: 300 } }
        MediaView {
            anchors.fill: parent
            telemetry: link.telemetry
            status: link.status
            online: link.online
            effects: root.effects
            extra: Plasmoid.configuration.extra
            onAction: (name, arg) => link.media(name)
        }
    }
}
