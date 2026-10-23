import QtQuick
import QtQuick.Window

// The compact overlay: a small core that stays above other windows and shows
// Jarvis's state at a glance. Drag it anywhere; a click opens Jarvis; the
// cross hides it. Its own controller runs only while it is shown.
Window {
    id: overlay
    required property var vs
    required property var voice
    property string quality: "balanced"
    property bool wanted: false       // shown when true
    property var graphicsInfo: null   // GraphicsInfo: gives this window, and only it, an alpha channel
    signal openRequested()
    signal closeRequested()

    width: 230
    height: 250
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    title: "J.A.R.V.I.S."
    // Hidden until completed: the alpha channel must be requested before the
    // window exists. Starts in the top right corner (X11; on Wayland KWin decides).
    visible: false
    Component.onCompleted: {
        x = Screen.desktopAvailableWidth - width - 24
        y = 56
        if (graphicsInfo) graphicsInfo.makeTranslucent(overlay)
        visible = Qt.binding(() => overlay.wanted)
    }

    HudController {
        id: ctl
        running: overlay.visible
        quality: overlay.quality === "cinematic" ? "balanced" : "low"
        refreshRate: overlay.graphicsInfo ? overlay.graphicsInfo.refreshRate : 0
        hudState: overlay.vs.state
        level: overlay.voice.outputLevel
        bands: overlay.voice.outputBands
        levelMeasured: overlay.voice.outputMeasured
        micLevel: overlay.voice.inputLevel
        streamRate: overlay.vs.streamRate
        streaming: overlay.vs.streaming
    }

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: Qt.rgba(0.01, 0.03, 0.07, 0.78)
        border.color: Qt.rgba(ctl.colorA.r, ctl.colorA.g, ctl.colorA.b, 0.35)
    }
    JarvisCore {
        anchors.fill: parent
        anchors.margins: 8
        anchors.bottomMargin: 34
        ctl: ctl
        vs: overlay.vs
        compact: true
        onActivated: overlay.openRequested()
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        text: "J.A.R.V.I.S. · " + JarvisTheme.stateName(overlay.vs.state).toUpperCase()
        color: ctl.colorA
        font.family: JarvisTheme.mono
        font.pixelSize: 11
        font.letterSpacing: 2
    }
    Text {
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
        text: "×"
        color: JarvisTheme.textDim
        font.pixelSize: 16
        TapHandler { onTapped: overlay.closeRequested() }
    }
    // The compositor moves the window (works on Wayland too).
    DragHandler {
        target: null
        onActiveChanged: if (active) overlay.startSystemMove()
    }
}
