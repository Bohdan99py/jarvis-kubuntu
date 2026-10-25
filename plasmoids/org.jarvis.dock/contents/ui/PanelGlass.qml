import QtQuick
import org.jarvis.visual as JV

// The glass of the whole panel (0.20): one Jarvis surface under the dock and
// under Plasma's own system tray and clock beside it. Created by the dock as
// the bottom-most child of the panel's containment, so every widget of the
// panel stands on it, and it covers Plasma's panel background (which stays:
// the panel's input region follows it). Plasma's widgets stay Plasma's (their
// popups, their models) — only the surface under them is Jarvis's.
Rectangle {
    id: glass
    property real surfaceOpacity: 0.9
    property real glow: 1
    property bool effects: true
    property int edge: Qt.BottomEdge
    property bool vertical: false

    anchors.fill: parent
    anchors.margins: -2   // Plasma's background reaches a little past the containment: cover its rim too
    z: -100
    // No rounder than Plasma's own panel background, whose corners would show past ours.
    radius: Math.min(6, (vertical ? width : height) / 2)
    // Opaque enough to hide Plasma's background under it.
    color: JV.JarvisTheme.alpha(JV.JarvisTheme.surface, Math.max(0.94, surfaceOpacity))
    border.color: JV.JarvisTheme.alpha(JV.JarvisTheme.edge, Math.min(1, JV.JarvisTheme.edge.a * Math.max(0.3, glow)))
    transformOrigin: edge === Qt.TopEdge ? Item.Top : edge === Qt.LeftEdge ? Item.Left : edge === Qt.RightEdge ? Item.Right : Item.Bottom
    opacity: 0
    scale: 0.96
    Component.onCompleted: { opacity = 1; scale = 1 }
    Behavior on opacity { enabled: glass.effects; NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
    Behavior on scale { enabled: glass.effects; NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }

    // A dark outline: the panel stays visible on a light wallpaper.
    Rectangle {
        anchors.fill: parent; anchors.margins: -1
        radius: parent.radius + 1
        color: "transparent"; border.color: JV.JarvisTheme.outline; z: -1
    }
    // The glow along the screen-side edge.
    Rectangle {
        id: edgeGlow
        visible: glass.glow > 0
        readonly property bool horizontal: glass.edge === Qt.BottomEdge || glass.edge === Qt.TopEdge
        width: horizontal ? parent.width * 0.6 : 1
        height: horizontal ? 1 : parent.height * 0.6
        x: horizontal ? (parent.width - width) / 2 : glass.edge === Qt.LeftEdge ? 0 : parent.width - 1
        y: horizontal ? (glass.edge === Qt.BottomEdge ? parent.height - 1 : 0) : (parent.height - height) / 2
        gradient: Gradient {
            orientation: edgeGlow.horizontal ? Gradient.Horizontal : Gradient.Vertical
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.5; color: JV.JarvisTheme.alpha(JV.JarvisTheme.cyan, Math.min(1, 0.7 * glass.glow)) }
            GradientStop { position: 1; color: "transparent" }
        }
    }
    // Corner ticks: the HUD's signature, in the very corners (the clock's text reaches close to the end).
    Repeater {
        model: glass.glow > 0 ? 4 : 0
        Item {
            required property int index
            width: 7; height: 7
            x: index % 2 ? glass.width - width - 3 : 3
            y: index < 2 ? 3 : glass.height - height - 3
            opacity: Math.min(1, 0.7 * glass.glow)
            Rectangle { width: 7; height: 1.5; color: JV.JarvisTheme.cyan; y: parent.index < 2 ? 0 : 5.5 }
            Rectangle { width: 1.5; height: 7; color: JV.JarvisTheme.cyan; x: parent.index % 2 ? 5.5 : 0 }
        }
    }
}
