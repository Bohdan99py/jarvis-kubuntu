import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.jarvis.visual as JV

// One application in the dock: its icon, a light arc under a running one
// (bright for the active window), dots for several windows, an amber pulse
// when it wants attention and a short ring when it starts. Every effect is
// an animator or static geometry, and none runs while nothing changes.
Item {
    id: task
    property int size: 40
    property string name
    property var icon
    property bool launcher: false     // pinned and not running
    property bool active: false
    property bool minimized: false
    property bool attention: false
    property bool starting: false
    property int windows: 0
    property bool effects: true
    signal activated()
    signal newInstance()
    signal menuRequested()

    implicitWidth: size
    implicitHeight: size
    readonly property bool hovered: mouse.containsMouse
    readonly property color tint: attention ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan

    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.onPressAction: activated()

    // The halo behind a hovered or active icon.
    Rectangle {
        anchors.centerIn: parent
        width: task.size * 0.92; height: width; radius: width / 2
        color: Qt.rgba(task.tint.r, task.tint.g, task.tint.b, task.hovered ? 0.16 : task.active ? 0.1 : 0)
        border.color: Qt.rgba(task.tint.r, task.tint.g, task.tint.b, task.hovered || task.active ? 0.45 * JV.JarvisTheme.glow : 0)
        Behavior on color { enabled: task.effects; ColorAnimation { duration: 150 } }
    }
    Kirigami.Icon {
        id: image
        anchors.centerIn: parent
        width: Math.round(task.size * 0.62); height: width
        source: task.icon
        opacity: task.minimized ? 0.6 : 1
        // The active icon a little larger; the hovered one larger still.
        scale: task.hovered ? 1.18 : task.active ? 1.08 : 1
        Behavior on scale { enabled: task.effects; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }
    // A light arc under a running application.
    Shape {
        anchors.fill: parent
        visible: !task.launcher
        opacity: task.active ? 1 : 0.55
        ShapePath {
            strokeColor: task.tint
            strokeWidth: task.active ? 2.5 : 1.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: task.width / 2; centerY: task.height * 0.5
                radiusX: task.size * 0.46; radiusY: radiusX
                startAngle: task.active ? 50 : 65
                sweepAngle: task.active ? 80 : 50
            }
        }
    }
    // Several windows: one dot each (up to four).
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        spacing: 2
        visible: task.windows > 1
        Repeater {
            model: Math.min(4, task.windows)
            Rectangle { width: 3; height: 3; radius: 1.5; color: task.tint }
        }
    }
    // Wants attention: an amber breath.
    Rectangle {
        anchors.centerIn: parent
        width: task.size; height: width; radius: width / 2
        color: "transparent"
        border.color: JV.JarvisTheme.amber
        border.width: 2
        visible: task.attention
        opacity: 0
        SequentialAnimation on opacity {
            running: task.attention && task.effects
            loops: Animation.Infinite
            NumberAnimation { to: 0.9; duration: 500 }
            NumberAnimation { to: 0.1; duration: 700 }
        }
    }
    // Starting: one ring that grows and fades.
    Rectangle {
        id: launchRing
        anchors.centerIn: parent
        width: task.size * 0.6; height: width; radius: width / 2
        color: "transparent"
        border.color: JV.JarvisTheme.cyan
        border.width: 2
        opacity: 0
        visible: opacity > 0
        ParallelAnimation {
            running: task.starting && task.effects
            loops: Animation.Infinite
            ScaleAnimator { target: launchRing; from: 0.7; to: 1.6; duration: 900; easing.type: Easing.OutCubic }
            OpacityAnimator { target: launchRing; from: 0.9; to: 0; duration: 900 }
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouseEvent => {
            if (mouseEvent.button === Qt.RightButton) task.menuRequested()
            else if (mouseEvent.button === Qt.MiddleButton) task.newInstance()
            else task.activated()
        }
    }
}
