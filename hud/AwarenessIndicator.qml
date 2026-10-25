import QtQuick
import Jarvis
import QtQuick.Controls

// What Jarvis knows about the user's activity, at a glance, and whether it is
// looking at the screen: "◎ CLion · programming in C++ · 70 %" and an eye that
// glows amber while a screenshot is taken and read, and stays lit while
// background screen analysis is on. Never hidden while the screen is observed.
Row {
    id: indicator
    required property var chat
    property var screen: null
    property bool showContext: true
    signal clicked()

    readonly property var ctx: { try { return JSON.parse(indicator.chat.context) } catch (e) { return {} } }
    readonly property bool known: !!ctx.available
    readonly property bool paused: (ctx.pausedUntil || 0) > 0
    readonly property bool watching: !!screen && (screen.capturing || screen.background)
    property bool contextHoverShown: false
    property bool eyeHoverShown: false
    // The tips are worded only while shown. ScreenAwareness has one NOTIFY signal
    // (`changed`) for everything; worded at creation, reading intervalMinutes
    // made QML apply Main.qml's still-pending binding of it, whose setter emits
    // `changed` — which this very binding had just subscribed to through
    // `capturing`: a binding loop (0.18–0.19.2).
    readonly property string contextTip: !contextHoverShown ? "" : known ? qsTr("What Jarvis infers from the focused window (confidence %1 %):\n%2")
                                                 .arg(Math.round((ctx.confidence || 0) * 100)).arg((ctx.evidence || []).join("\n"))
                                           : ""
    readonly property string eyeTip: !eyeHoverShown ? "" : screen && screen.capturing
                                     ? qsTr("Jarvis is taking a screenshot of the window and reading its text on this computer.")
                                     : qsTr("Background screen analysis is on: at most every %1 min. Click to pause.").arg(screen ? screen.intervalMinutes : 0)

    spacing: 6
    height: 22

    Rectangle {
        visible: indicator.showContext && (indicator.known || indicator.paused)
        height: 22
        width: contextText.implicitWidth + 16
        radius: 3
        color: Qt.rgba(0.02, 0.07, 0.12, 0.85)
        border.color: indicator.paused ? JarvisTheme.amber : JarvisTheme.line
        Text {
            id: contextText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 320)
            elide: Text.ElideRight
            text: indicator.paused ? qsTr("observation paused")
                : "◎ " + indicator.ctx.app + " · " + indicator.ctx.label + " · " + Math.round((indicator.ctx.confidence || 0) * 100) + " %"
            color: indicator.paused ? JarvisTheme.amber : JarvisTheme.textDim
            font.family: JarvisTheme.mono
            font.pixelSize: 10
        }
        HoverHandler { id: contextHover; onHoveredChanged: indicator.contextHoverShown = hovered }
        TapHandler { onTapped: indicator.clicked() }
        // Its own tool tip (not the window's shared, attached one: two attachees writing
        // the shared tip's text re-entered each other's binding — a binding loop).
        ToolTip {
            visible: contextHover.hovered && indicator.known
            delay: 400
            text: indicator.contextTip
        }
    }
    // The eye: screen observation, impossible to miss.
    Rectangle {
        visible: indicator.watching
        height: 22
        width: eyeText.implicitWidth + 16
        radius: 3
        color: indicator.screen && indicator.screen.capturing ? Qt.rgba(1, 0.7, 0.28, 0.22) : Qt.rgba(1, 0.7, 0.28, 0.08)
        border.color: JarvisTheme.amber
        Text {
            id: eyeText
            anchors.centerIn: parent
            text: indicator.screen && indicator.screen.capturing ? qsTr("◉ READING SCREEN") : qsTr("◉ SCREEN")
            color: JarvisTheme.amber
            font.family: JarvisTheme.mono
            font.pixelSize: 10
            font.bold: true
            SequentialAnimation on opacity {
                running: !!indicator.screen && indicator.screen.capturing
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { to: 0.35; duration: 380 }
                NumberAnimation { to: 1; duration: 380 }
            }
        }
        HoverHandler { id: eyeHover; onHoveredChanged: indicator.eyeHoverShown = hovered }
        TapHandler { onTapped: indicator.clicked() }
        ToolTip {
            visible: eyeHover.hovered
            delay: 300
            text: indicator.eyeTip
        }
    }
}
