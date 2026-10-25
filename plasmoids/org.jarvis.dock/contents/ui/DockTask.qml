import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.jarvis.visual as JV

// One application in the dock (0.19.3): its icon, the light line of a running
// application on the screen-edge side (long and bright for the active one),
// one tick per window (up to four), an amber breath while it wants attention,
// a ring while it starts and a short pulse when another window of it opens.
// The dock sets `zoom` and `shift` from the pointer (magnification); every
// effect is an animator or static geometry, and nothing runs while nothing
// changes.
Item {
    id: task
    property int size: 40                 // the cell (the panel's thickness)
    property real iconFraction: 0.62      // the icon at rest, of the cell
    property real zoom: 1                 // the dock's magnification for this icon
    property real shift: 0                // neighbours step aside a little
    property string name
    property var icon
    property bool launcher: false         // pinned and not running
    property bool active: false
    property bool minimized: false
    property bool attention: false
    property bool starting: false
    property int windows: 0
    property bool effects: true
    property real glow: 1
    property int edge: Qt.BottomEdge      // where the screen edge is: the line goes there
    property bool dragging: false
    property bool dropHover: false
    property string toolTipSub: ""
    property bool toolTipActive: true
    signal activated()
    signal newInstance()
    signal menuRequested()

    implicitWidth: size
    implicitHeight: size
    readonly property bool hovered: mouse.containsMouse
    readonly property color tint: attention ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
    readonly property bool horizontalEdge: edge === Qt.BottomEdge || edge === Qt.TopEdge

    Accessible.role: Accessible.Button
    Accessible.name: name + (windows > 1 ? " (" + windows + ")" : "")
    Accessible.onPressAction: activated()

    // Another window of the application: one pulse.
    property int lastWindows: 0
    onWindowsChanged: {
        if (windows > lastWindows && lastWindows > 0 && effects)
            pulseAnim.restart()
        lastWindows = windows
    }
    Component.onCompleted: lastWindows = windows

    Item {
        id: body
        anchors.fill: parent
        transform: Translate {
            x: task.horizontalEdge ? task.shift : 0
            y: task.horizontalEdge ? 0 : task.shift
        }
        // The halo behind a hovered, active or drop-target icon.
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(task.size - 4, image.width * task.zoom + 12); height: width; radius: width / 2
            color: JV.JarvisTheme.alpha(task.tint, task.dropHover ? 0.28 : task.hovered ? 0.16 : task.active ? 0.1 : 0)
            border.color: JV.JarvisTheme.alpha(task.tint, task.dropHover || task.hovered || task.active ? Math.min(1, 0.45 * task.glow) : 0)
            Behavior on color { enabled: task.effects; ColorAnimation { duration: 150 } }
        }
        Kirigami.Icon {
            id: image
            anchors.centerIn: parent
            // Grows from the screen edge, like a dock's icon.
            anchors.verticalCenterOffset: task.horizontalEdge ? (task.edge === Qt.BottomEdge ? -1 : 1) * (task.zoom - 1) * width * 0.25 : 0
            anchors.horizontalCenterOffset: task.horizontalEdge ? 0 : (task.edge === Qt.RightEdge ? -1 : 1) * (task.zoom - 1) * width * 0.25
            width: Math.round(task.size * task.iconFraction); height: width
            source: task.icon
            fallback: "application-x-executable"
            opacity: task.dragging ? 0.4 : task.minimized ? 0.6 : 1
            scale: task.zoom * (mouse.pressed ? 0.92 : 1)
            Behavior on scale { enabled: task.effects; NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        }
    }
    // The light line of a running application, on the screen-edge side.
    Rectangle {
        id: line
        visible: !task.launcher
        readonly property real length: task.active ? task.size * 0.46 : task.size * 0.16
        width: task.horizontalEdge ? length : 3
        height: task.horizontalEdge ? 3 : length
        radius: 1.5
        x: task.horizontalEdge ? (task.width - width) / 2 : task.edge === Qt.LeftEdge ? 1 : task.width - width - 1
        y: task.horizontalEdge ? (task.edge === Qt.BottomEdge ? task.height - height - 1 : 1) : (task.height - height) / 2
        color: task.tint
        opacity: task.active ? 1 : task.minimized ? 0.35 : 0.65
        Behavior on width { enabled: task.effects && task.horizontalEdge; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on height { enabled: task.effects && !task.horizontalEdge; NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        // The active one glows: a soft wider line under it.
        Rectangle {
            visible: task.active && task.glow > 0
            anchors.centerIn: parent
            width: parent.width + (task.horizontalEdge ? 8 : 4); height: parent.height + (task.horizontalEdge ? 4 : 8)
            radius: height / 2
            color: JV.JarvisTheme.alpha(task.tint, 0.25 * task.glow)
            z: -1
        }
    }
    // Several windows: one tick each (up to four) beside the line.
    Grid {
        visible: task.windows > 1
        rows: task.horizontalEdge ? 1 : 0
        columns: task.horizontalEdge ? 0 : 1
        spacing: 3
        x: task.horizontalEdge ? line.x + line.width + 4 : line.x
        y: task.horizontalEdge ? line.y : line.y + line.height + 4
        Repeater {
            model: Math.max(0, Math.min(3, task.windows - 1))
            Rectangle { width: 3; height: 3; radius: 1.5; color: task.tint; opacity: 0.8 }
        }
    }
    // Wants attention: an amber breath.
    Rectangle {
        anchors.centerIn: parent
        width: task.size - 2; height: width; radius: width / 2
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
    // Starting, or one more window: a ring that grows and fades.
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
        ParallelAnimation {
            id: pulseAnim
            ScaleAnimator { target: launchRing; from: 0.8; to: 1.5; duration: 600; easing.type: Easing.OutCubic }
            OpacityAnimator { target: launchRing; from: 0.8; to: 0; duration: 600 }
        }
    }
    // Plasma's tool tip: its own window beside the panel. (A Qt Quick tool tip lives
    // inside the panel's window, lands on the icon itself and swallows the click.)
    PlasmaCore.ToolTipArea {
        anchors.fill: parent
        mainText: task.name
        subText: task.toolTipSub
        active: task.toolTipActive && !task.dragging
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
            // Long press (touch): the menu.
            onPressAndHold: mouseEvent => { if (mouseEvent.button === Qt.LeftButton && !task.dragging) task.menuRequested() }
        }
    }
}
