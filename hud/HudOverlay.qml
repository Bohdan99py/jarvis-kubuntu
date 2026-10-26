import QtQuick
import Jarvis
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// The overlay: Jarvis above any application (Ctrl+J), without switching to the
// main window. It continues the same conversation (one ChatModel), knows which
// application the user came from (jarvisd keeps it: Jarvis's own windows never
// replace it) and says exactly what it will see.
//
//   Compact  — a small panel: the core, the context, the question, the answer.
//   Expanded — the whole conversation, code blocks, more room (Ctrl+E).
//   Pinned   — the 0.17 mini core that stays on screen (Ctrl+Shift+O); a click opens the panel.
//
// Esc closes the panel (or stops the answer being written); the desktop gives
// the focus back to the previous window. On KDE Wayland the overlay is a
// layer-shell surface (OverlayLayer.qml) so it opens on the active screen and
// takes the keyboard; elsewhere it is an always-on-top frameless window.
Window {
    id: overlay
    required property var vs
    required property var voice
    required property var chat
    property var screenAware: null       // ScreenAwareness
    property var settings: null          // AppSettings
    property string quality: "balanced"
    property bool wanted: false          // pinned mini core
    property var graphicsInfo: null      // GraphicsInfo: gives this window, and only it, an alpha channel
    property bool layer: false           // a layer-shell surface (set by OverlayLayer.qml)
    signal openRequested()               // the main window
    signal closeRequested()              // unpin
    signal voiceSetupRequested()

    property bool open: false
    property bool expanded: false
    property bool useScreen: false
    readonly property bool effects: quality !== "low" && quality !== "off"
    readonly property var ctx: { try { return JSON.parse(overlay.chat.context) } catch (e) { return {} } }
    readonly property bool windowKnown: !!ctx.available && !ctx.excluded
    readonly property bool screenAllowed: !!overlay.settings && overlay.settings.awarenessScreen

    readonly property int compactHeight: 28 + 46 + 10 + composer.implicitHeight + 10 + 14
                                         + (answerArea.visible ? answerArea.Layout.preferredHeight + 10 : 0)
                                         + (contextLine.visible ? contextLine.implicitHeight + 10 : 0)
    // 0.19.3: the size changes smoothly — the panel animates to its new size inside the
    // window; the window takes the larger of the two, so growing never clips and
    // shrinking follows the panel.
    readonly property real targetWidth: open ? (expanded ? 780 : 620) : 230
    readonly property real targetHeight: open ? (expanded ? Math.min(700, Math.max(420, (screen ? screen.height : 900) * 0.78)) : compactHeight) : 250
    property real panelWidth: targetWidth
    property real panelHeight: targetHeight
    Behavior on panelWidth { enabled: overlay.open && overlay.effects; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on panelHeight { enabled: overlay.open && overlay.effects; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    width: open ? Math.ceil(Math.max(targetWidth, panelWidth)) : targetWidth
    height: open ? Math.ceil(Math.max(targetHeight, panelHeight)) : targetHeight
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("Jarvis — overlay")
    // Hidden until completed: the alpha channel must be requested before the window exists.
    visible: false
    Component.onCompleted: {
        if (graphicsInfo) graphicsInfo.makeTranslucent(overlay)
        visible = Qt.binding(() => overlay.open || overlay.wanted)
    }

    // Opens the panel: from Ctrl+J, the tray, a notification. `token`: an XDG
    // activation token from the desktop; `prompt`: a question to ask at once.
    function showOverlay(token, prompt) {
        overlay.chat.refreshContext()
        if (token && token.length > 0)
            desktopBridge.prepareActivation(token)
        const wasOpen = overlay.open
        overlay.open = true
        if (!overlay.layer && !wasOpen) {
            const s = overlay.screen
            if (s) {
                x = s.virtualX + Math.round((s.width - width) / 2)
                y = s.virtualY + Math.round(s.height * 0.1)
            }
        }
        if (!wasOpen && overlay.effects) unfold.restart()
        else panel.reveal = 1
        overlay.show()
        overlay.raise()
        overlay.requestActivate()
        composer.focusInput()
        if (prompt && prompt.length > 0)
            Qt.callLater(overlay.ask, prompt, overlay.screenAllowed)
    }
    function hideOverlay() {
        overlay.open = false
        overlay.useScreen = false
    }
    function ask(text, withScreen) {
        if (overlay.chat.busy) return
        overlay.chat.sendWithContext(text, [], true, withScreen)
    }

    Shortcut {
        sequence: "Escape"
        enabled: overlay.open
        onActivated: overlay.chat.busy ? overlay.chat.stop() : overlay.hideOverlay()
    }
    Shortcut { sequence: "Ctrl+E"; enabled: overlay.open; onActivated: overlay.expanded = !overlay.expanded }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: overlay.open && overlay.screenAllowed; onActivated: overlay.useScreen = !overlay.useScreen }

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

    // ---------- Pinned: the mini core ----------
    Item {
        anchors.fill: parent
        visible: !overlay.open
        Rectangle {
            anchors.fill: parent
            radius: 22
            color: Qt.rgba(0.01, 0.03, 0.07, Math.max(0.78, JarvisTheme.surfaceAlpha - 0.05))
            border.color: Qt.rgba(ctl.colorA.r, ctl.colorA.g, ctl.colorA.b, 0.35)
        }
        JarvisCore {
            anchors.fill: parent
            anchors.margins: 8
            anchors.bottomMargin: 34
            ctl: ctl
            vs: overlay.vs
            compact: true
            onActivated: overlay.showOverlay("", "")
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
        // The compositor moves the window (works on Wayland too); not a layer surface.
        DragHandler {
            target: null
            enabled: !overlay.layer
            onActiveChanged: if (active) overlay.startSystemMove()
        }
    }

    // ---------- The panel ----------
    Item {
        id: panel
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: overlay.panelWidth
        height: overlay.panelHeight
        visible: overlay.open
        property real reveal: 1
        function ease(v) { return 1 - Math.pow(1 - Math.max(0, Math.min(1, v)), 3) }
        NumberAnimation { id: unfold; target: panel; property: "reveal"; from: 0; to: 1; duration: 460; easing.type: Easing.Linear }

        // A line draws across the top, the frame opens under it, the content fades in.
        Rectangle {
            visible: panel.reveal < 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * panel.ease(panel.reveal / 0.35)
            height: 2
            color: JarvisTheme.cyan
        }
        Rectangle {
            id: frame
            width: parent.width
            height: parent.height * panel.ease((panel.reveal - 0.15) / 0.45)
            opacity: panel.ease((panel.reveal - 0.1) / 0.3)
            // The same surface as the dock, the launcher and the desktop widgets.
            radius: JarvisTheme.radius + 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0.03, 0.09, 0.16, Math.max(0.93, JarvisTheme.surfaceAlpha)) }
                GradientStop { position: 1; color: Qt.rgba(0.01, 0.04, 0.08, Math.max(0.91, JarvisTheme.surfaceAlpha)) }
            }
            border.width: 1
            border.color: Qt.rgba(ctl.colorA.r, ctl.colorA.g, ctl.colorA.b, 0.55)
            Rectangle { anchors.fill: parent; anchors.margins: -1; radius: parent.radius + 1; color: "transparent"; border.color: JarvisTheme.outline; z: -1 }
            Repeater {
                model: 4
                delegate: Item {
                    id: bracket
                    required property int index
                    readonly property bool atRight: index % 2 === 1
                    readonly property bool atBottom: index > 1
                    width: 12; height: 12
                    x: atRight ? frame.width - width - 5 : 5
                    y: atBottom ? frame.height - height - 5 : 5
                    Rectangle { x: bracket.atRight ? bracket.width - 2 : 0; width: 2; height: bracket.height; color: ctl.colorA }
                    Rectangle { y: bracket.atBottom ? bracket.height - 2 : 0; width: bracket.width; height: 2; color: ctl.colorA }
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10
            opacity: panel.ease((panel.reveal - 0.5) / 0.5)

            // Header: the core, the state, the controls.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item {
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    JarvisCore {
                        anchors.fill: parent
                        ctl: ctl
                        vs: overlay.vs
                        compact: true
                        onActivated: composer.toggleMic()
                    }
                }
                ColumnLayout {
                    spacing: 1
                    Text { text: "J.A.R.V.I.S."; color: JarvisTheme.ice; font.family: JarvisTheme.mono; font.pixelSize: 13; font.bold: true; font.letterSpacing: 3 }
                    Text {
                        text: overlay.chat.busy ? (overlay.vs.routes.length > 0 ? overlay.vs.routes.map(JarvisTheme.routeName).join(" › ") : qsTr("thinking…"))
                                                : JarvisTheme.stateName(overlay.vs.state)
                        color: JarvisTheme.stateColor(overlay.vs.state)
                        font.family: JarvisTheme.mono; font.pixelSize: JarvisTheme.fontSmall; font.letterSpacing: 1
                    }
                }
                Item { Layout.fillWidth: true }
                AwarenessIndicator {
                    chat: overlay.chat
                    screen: overlay.screenAware
                    showContext: false
                }
                HudButton {
                    text: "◉"
                    on: overlay.useScreen
                    enabled: overlay.screenAllowed
                    tip: overlay.screenAllowed ? (overlay.useScreen ? qsTr("The window's content goes with the question · Ctrl+Shift+S") : qsTr("Let Jarvis read the window with the question · Ctrl+Shift+S"))
                                               : qsTr("Screen awareness is off (Settings → Memory and privacy)")
                    onClicked: overlay.useScreen = !overlay.useScreen
                    Accessible.name: qsTr("Screen")
                }
                HudButton {
                    text: overlay.expanded ? "⤡" : "⤢"
                    on: overlay.expanded
                    tip: overlay.expanded ? qsTr("Compact · Ctrl+E") : qsTr("Expand: the whole conversation · Ctrl+E")
                    onClicked: overlay.expanded = !overlay.expanded
                    Accessible.name: qsTr("Expand")
                }
                HudButton {
                    text: "⌂"
                    tip: qsTr("Open the main window")
                    onClicked: { overlay.hideOverlay(); overlay.openRequested() }
                    Accessible.name: qsTr("Open the main window")
                }
                HudButton {
                    text: "×"
                    tip: qsTr("Close · Esc")
                    onClicked: overlay.hideOverlay()
                    Accessible.name: qsTr("Close")
                }
            }

            // Honest about what goes with the question.
            Text {
                id: contextLine
                Layout.fillWidth: true
                visible: overlay.open
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: JarvisTheme.textDim
                font.family: JarvisTheme.mono
                font.pixelSize: JarvisTheme.fontSmall
                text: {
                    if ((overlay.ctx.pausedUntil || 0) > 0)
                        return qsTr("Observation is paused: the question goes without desktop context.")
                    if (overlay.ctx.excluded)
                        return qsTr("The previous window is private or excluded: Jarvis does not look at it.")
                    if (!overlay.windowKnown)
                        return overlay.settings && !overlay.settings.awarenessWindow
                               ? qsTr("Jarvis does not know which application you came from (Settings → Memory and privacy → Allow Jarvis to see the active window).")
                               : qsTr("No application context yet.")
                    const what = overlay.ctx.app + " — " + overlay.ctx.label + " (" + Math.round((overlay.ctx.confidence || 0) * 100) + " %)"
                    const sees = overlay.useScreen ? (overlay.settings && overlay.settings.awarenessCloudImages ? qsTr("title, window text and a screenshot") : qsTr("title and window text (read on this computer)"))
                                                   : qsTr("only the application and window title")
                    return qsTr("Context: %1 · Jarvis gets %2").arg(what).arg(sees)
                }
            }

            // Expanded: the whole conversation.
            ChatView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: overlay.expanded
                chat: overlay.chat
                vs: overlay.vs
                effects: overlay.effects
            }

            // Compact: the answer being written, or the last one.
            Rectangle {
                id: answerArea
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, answerText.implicitHeight + 20)
                visible: !overlay.expanded && (overlay.chat.busy || overlay.chat.liveReply.length > 0)
                radius: 3
                color: Qt.rgba(0.02, 0.06, 0.11, 0.8)
                border.color: overlay.chat.busy ? Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.6) : JarvisTheme.line
                Rectangle { width: 2; height: parent.height - 10; y: 5; color: JarvisTheme.cyan; opacity: 0.8 }
                Flickable {
                    id: answerFlick
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 14
                    anchors.rightMargin: copyAnswer.visible ? copyAnswer.width + 14 : 10
                    clip: true
                    contentHeight: answerText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    onContentHeightChanged: if (overlay.chat.busy) contentY = Math.max(0, contentHeight - height)
                    ScrollBar.vertical: ScrollBar {}
                    TextEdit {
                        id: answerText
                        width: answerFlick.width - 8
                        text: overlay.chat.liveReply.length > 0 ? desktopBridge.safeMarkdown(overlay.chat.liveReply)
                                                                : (overlay.chat.stopping ? qsTr("stopping…") : qsTr("thinking…"))
                        textFormat: overlay.chat.liveReply.length > 0 ? TextEdit.MarkdownText : TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: overlay.chat.liveReply.length > 0 ? JarvisTheme.text : JarvisTheme.textDim
                        selectionColor: JarvisTheme.cyan
                        font.pixelSize: 14
                        onLinkActivated: link => { if (/^https?:\/\//.test(link)) Qt.openUrlExternally(link) }
                    }
                }
                HudButton {
                    id: copyAnswer
                    visible: !overlay.chat.busy && overlay.chat.liveReply.length > 0
                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
                    implicitHeight: 24
                    text: qsTr("copy")
                    onClicked: overlay.chat.copyText(overlay.chat.liveReply)
                }
            }

            ChatComposer {
                id: composer
                Layout.fillWidth: true
                chat: overlay.chat
                voice: overlay.voice
                compact: !overlay.expanded
                attachable: false
                showHints: false
                effects: overlay.effects
                withContext: true
                withScreen: overlay.useScreen
                agentMode: overlay.settings ? overlay.settings.agentMode : ""
                placeholder: overlay.windowKnown ? qsTr("Ask about %1…").arg(overlay.ctx.app) : qsTr("Ask Jarvis…")
                onVoiceSetupRequested: overlay.voiceSetupRequested()
                onSent: overlay.useScreen = false
            }

            // Keys and the pause.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Esc — close · Enter — send · Ctrl+E — expand · Ctrl+Shift+S — screen")
                    color: JarvisTheme.textDim
                    font.family: JarvisTheme.mono
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
                Text {
                    visible: !!overlay.settings && overlay.settings.awarenessLevel !== "off"
                    text: (overlay.ctx.pausedUntil || 0) > 0 ? qsTr("resume observation") : qsTr("pause observation for 1 h")
                    color: pauseMouse.containsMouse ? JarvisTheme.amber : JarvisTheme.textDim
                    font.family: JarvisTheme.mono
                    font.pixelSize: 9
                    MouseArea {
                        id: pauseMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: overlay.chat.pauseAwareness((overlay.ctx.pausedUntil || 0) > 0 ? 0 : 60)
                    }
                }
            }
        }
    }
}
