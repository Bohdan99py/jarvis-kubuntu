import QtQuick
import Jarvis
import QtQuick.Controls
import QtQuick.Layouts

// Where the user writes. Multi-line (it grows up to six lines), Enter sends,
// Shift+Enter starts a new line, Esc stops the answer being written, ↑ in an
// empty field brings the last message back, Ctrl+Shift+M dictates, Ctrl+O
// attaches. Its edges react: corner brackets light up with the focus, a level
// bar on the left follows the microphone, a scanner runs along the bottom while
// Jarvis works and a pulse crosses the top when a message leaves.
FocusScope {
    id: composer
    required property var chat
    required property var voice
    property bool effects: true
    property bool compact: false          // the overlay: smaller
    property bool attachable: true       // the attach button (the main window has a file dialog)
    property bool showHints: true        // the key hints under the field
    // Files waiting to go with the next message (URLs).
    property var pendingFiles: []
    // The overlay sends with the desktop context.
    property bool withContext: false
    property bool withScreen: false
    property string placeholder: qsTr("Write a message…")
    property string lastSent: ""
    readonly property alias text: input.text
    signal attachRequested()
    signal voiceSetupRequested()
    signal sent(string text)

    function focusInput() { input.forceActiveFocus() }
    function setText(t) { input.text = t; input.cursorPosition = input.length; input.forceActiveFocus() }
    function attach(urls) {
        const next = composer.pendingFiles.slice()
        for (let i = 0; i < urls.length && next.length < 8; ++i)
            if (next.map(String).indexOf(String(urls[i])) < 0)
                next.push(urls[i])
        composer.pendingFiles = next
    }
    function fileName(url) { return decodeURIComponent(String(url).split("/").pop()) }
    function submit() {
        const t = input.text.trim()
        if (composer.chat.busy || t.length > 4096)
            return
        if (t.length === 0 && composer.pendingFiles.length === 0 && !composer.withScreen)
            return
        if (composer.withContext || composer.withScreen)
            composer.chat.sendWithContext(t, composer.pendingFiles, composer.withContext, composer.withScreen)
        else
            composer.chat.sendWithFiles(t, composer.pendingFiles)
        composer.lastSent = t
        composer.sent(t.length > 0 ? t : composer.pendingFiles.map(composer.fileName).join(", "))
        input.text = ""
        composer.pendingFiles = []
        if (composer.effects) sendPulse.restart()
    }
    function toggleMic() {
        if (!composer.voice.ready) composer.voiceSetupRequested()
        else if (composer.voice.recording) composer.voice.stop()
        else composer.voice.listen()
    }

    implicitHeight: column.implicitHeight

    Shortcut { sequence: "Ctrl+Shift+M"; enabled: composer.visible; onActivated: composer.toggleMic() }
    Shortcut { sequence: "Ctrl+O"; enabled: composer.visible && composer.attachable; onActivated: composer.attachRequested() }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6

        // Files waiting to be sent.
        Flow {
            Layout.fillWidth: true
            visible: composer.pendingFiles.length > 0
            spacing: 6
            Repeater {
                model: composer.pendingFiles
                Rectangle {
                    id: fileChip
                    required property var modelData
                    required property int index
                    width: chipRow.implicitWidth + 16
                    height: 26
                    radius: 3
                    color: Qt.rgba(0.04, 0.12, 0.2, 0.9)
                    border.color: JarvisTheme.line
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: "▣ " + composer.fileName(fileChip.modelData); color: JarvisTheme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: "×"; color: removeMouse.containsMouse ? JarvisTheme.cyan : JarvisTheme.textDim; font.pixelSize: 15
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                id: removeMouse
                                anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { const next = composer.pendingFiles.slice(); next.splice(fileChip.index, 1); composer.pendingFiles = next }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            HudButton {
                visible: composer.attachable
                text: "▣"
                tip: qsTr("Attach files: photos, PDF datasheets, code, KiCad and SimulIDE schematics · Ctrl+O. You can also drop them onto the chat.")
                enabled: !composer.chat.busy
                onClicked: composer.attachRequested()
                Accessible.name: qsTr("Attach files")
            }

            // The field with its reactive edges.
            Item {
                id: field
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(composer.compact ? 110 : 150, Math.max(composer.compact ? 40 : 44, input.contentHeight + 20))
                Behavior on Layout.preferredHeight { enabled: composer.effects; NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                readonly property color edge: composer.chat.busy ? JarvisTheme.cyan : input.activeFocus ? JarvisTheme.cyan : JarvisTheme.line

                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    color: Qt.rgba(0.01, 0.04, 0.08, 0.85)
                    border.width: 1
                    border.color: input.activeFocus ? Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.6) : JarvisTheme.line
                    Behavior on border.color { enabled: composer.effects; ColorAnimation { duration: 160 } }
                }
                // Corner brackets: brighter with the focus.
                Repeater {
                    model: 4
                    delegate: Item {
                        id: bracket
                        required property int index
                        readonly property bool atRight: index % 2 === 1
                        readonly property bool atBottom: index > 1
                        width: 10; height: 10
                        x: atRight ? field.width - width : 0
                        y: atBottom ? field.height - height : 0
                        opacity: input.activeFocus || composer.chat.busy ? 1 : 0.45
                        Behavior on opacity { enabled: composer.effects; NumberAnimation { duration: 160 } }
                        Rectangle { x: bracket.atRight ? bracket.width - 2 : 0; width: 2; height: bracket.height; color: field.edge }
                        Rectangle { y: bracket.atBottom ? bracket.height - 2 : 0; width: bracket.width; height: 2; color: field.edge }
                    }
                }
                // Microphone level on the left edge while dictating.
                Rectangle {
                    visible: composer.voice.recording
                    x: 0
                    width: 2
                    height: field.height * Math.max(0.1, Math.min(1, composer.voice.inputLevel * 1.4))
                    anchors.verticalCenter: parent.verticalCenter
                    color: JarvisTheme.ice
                }
                // A scanner along the bottom while Jarvis works.
                Rectangle {
                    visible: composer.chat.busy && composer.effects
                    y: field.height - 1
                    height: 1
                    width: field.width * 0.2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 0.5; color: JarvisTheme.cyan }
                        GradientStop { position: 1; color: "transparent" }
                    }
                    SequentialAnimation on x {
                        running: composer.chat.busy && composer.effects && composer.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: 0; to: field.width * 0.8; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: field.width * 0.8; to: 0; duration: 900; easing.type: Easing.InOutSine }
                    }
                }
                // A pulse that crosses the top edge when a message leaves.
                Rectangle {
                    id: pulse
                    y: 0
                    height: 2
                    width: field.width * 0.35
                    opacity: 0
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 0.7; color: JarvisTheme.amber }
                        GradientStop { position: 1; color: JarvisTheme.ice }
                    }
                    ParallelAnimation {
                        id: sendPulse
                        NumberAnimation { target: pulse; property: "x"; from: -pulse.width; to: field.width; duration: 520; easing.type: Easing.OutCubic }
                        SequentialAnimation {
                            NumberAnimation { target: pulse; property: "opacity"; from: 0; to: 1; duration: 80 }
                            NumberAnimation { target: pulse; property: "opacity"; to: 0; duration: 440 }
                        }
                    }
                }

                ScrollView {
                    id: scroll
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: input
                        focus: true
                        placeholderText: composer.voice.recording ? qsTr("Listening… speak now") : composer.placeholder
                        placeholderTextColor: JarvisTheme.textDim
                        color: JarvisTheme.text
                        font.pixelSize: Theme.fontSize
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        persistentSelection: true
                        selectionColor: JarvisTheme.cyan
                        selectedTextColor: JarvisTheme.void_
                        textFormat: TextEdit.PlainText
                        verticalAlignment: TextEdit.AlignVCenter
                        background: null
                        Accessible.name: qsTr("Message to Jarvis")
                        Keys.onPressed: event => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                composer.submit()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape && composer.chat.busy) {
                                composer.chat.stop()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up && input.length === 0 && composer.lastSent.length > 0) {
                                composer.setText(composer.lastSent)
                                event.accepted = true
                            }
                        }
                    }
                }
                // How much room is left, close to the limit.
                Text {
                    visible: input.length > 3500
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    text: input.length + " / 4096"
                    color: input.length > 4096 ? JarvisTheme.red : JarvisTheme.textDim
                    font.family: JarvisTheme.mono
                    font.pixelSize: 9
                }
            }

            HudButton {
                text: composer.voice.recording ? "■" : "🎙"
                on: composer.voice.recording
                tip: composer.voice.recording ? qsTr("Stop dictation · Ctrl+Shift+M") : qsTr("Dictate · Ctrl+Shift+M")
                enabled: !composer.voice.busy || composer.voice.recording
                onClicked: composer.toggleMic()
                Accessible.name: qsTr("Microphone")
            }
            // Send, or stop the answer being written.
            HudButton {
                Layout.preferredWidth: Math.max(implicitWidth, composer.compact ? 64 : 84)
                text: composer.chat.busy ? (composer.chat.stopping ? "…" : qsTr("■ Stop")) : qsTr("Send")
                primary: !composer.chat.busy
                danger: composer.chat.busy
                tip: composer.chat.busy ? qsTr("Stop the answer · Esc") : qsTr("Send · Enter (Shift+Enter: new line)")
                enabled: composer.chat.busy ? !composer.chat.stopping
                                            : (input.text.trim().length > 0 || composer.pendingFiles.length > 0 || composer.withScreen) && input.length <= 4096
                onClicked: composer.chat.busy ? composer.chat.stop() : composer.submit()
                Accessible.name: composer.chat.busy ? qsTr("Stop") : qsTr("Send")
            }
        }

        // Status under the field: dictation, the voice engine, keys.
        Text {
            Layout.fillWidth: true
            visible: composer.voice.busy || composer.voice.recording || (input.activeFocus && input.length === 0 && composer.showHints)
            text: composer.voice.busy || composer.voice.recording ? composer.voice.status
                : qsTr("Enter — send · Shift+Enter — new line · Esc — stop · ↑ — last message · Ctrl+Shift+M — dictate")
            color: composer.voice.busy || composer.voice.recording ? JarvisTheme.cyan : JarvisTheme.textDim
            font.family: JarvisTheme.mono
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
