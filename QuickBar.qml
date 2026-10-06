import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Jarvis

Window {
    id: bar
    required property var chat
    required property var voice
    required property var desktop
    required property var settings
    signal voiceSetupRequested()
    property string actionStatus: ""
    readonly property var suggestions: {
        try {
            const all = (JSON.parse(chat.memory).activity || {}).suggestions || []
            return all.filter(s => QuickCommands.base.indexOf(s.id) < 0).slice(0, 4)
        } catch (e) {
            return []
        }
    }
    width: 760; height: 470
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("Jarvis — quick commands")
    function reveal() {
        x = screen.virtualX + (screen.width - width) / 2
        y = screen.virtualY + screen.height * 0.18
        show(); raise(); requestActivate(); query.forceActiveFocus()
    }
    function submit() {
        const text=query.text.trim()
        if (!text || chat.busy) return
        if (text.startsWith("/web ")) { webSearch(text.substring(5)) }
        else { chat.send(text); actionStatus="" }
        query.text=""
    }
    function webSearch(text) {
        if (!text.trim()) return
        chat.recordAction("web", QuickCommands.label("web"))
        desktop.searchWeb(text)
        actionStatus=qsTr("Search opened in the browser")
    }
    function runAction(id, label) {
        actionStatus = QuickCommands.run(id, label, chat, desktop, settings.uiLanguage)
    }
    Shortcut { sequence: "Escape"; onActivated: bar.hide() }
    Rectangle {
        anchors.fill: parent; radius: 24; color: Theme.panel; border.color: "#36556b"; border.width: 1
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 22; spacing: 14
            RowLayout {
                Label { text: "JARVIS"; color: Theme.accent; font.letterSpacing: 4; font.bold: true }
                Label { text: qsTr("Quick access"); color: Theme.textDim }
                Item { Layout.fillWidth: true }
                DarkButton { text: qsTr("Open chat"); onClicked: { desktop.ShowMain(); bar.hide() } }
                DarkButton { text: "×"; onClicked: bar.hide(); Accessible.name: qsTr("Close quick access") }
            }
            RowLayout {
                TextField {
                    id: query
                    Layout.fillWidth: true; Layout.preferredHeight: 56
                    font.pixelSize: 20; color: Theme.text; placeholderTextColor: Theme.textDim
                    placeholderText: qsTr("Ask, run a command or /web search…")
                    maximumLength: 4096; selectByMouse: true
                    background: Rectangle { color: Theme.bg; radius: 14; border.color: query.activeFocus ? Theme.accent : Theme.border }
                    onAccepted: bar.submit()
                }
                DarkButton {
                    text: voice.recording ? "■" : qsTr("Microphone")
                    enabled: !voice.busy || voice.recording
                    onClicked: { if(!voice.ready) bar.voiceSetupRequested(); else if(voice.recording) voice.stop(); else voice.listen() }
                }
                DarkButton { text: "↵"; primary: true; enabled: !chat.busy && query.text.trim().length>0; onClicked: bar.submit(); Accessible.name: qsTr("Send") }
            }
            Flow {
                Layout.fillWidth: true; spacing: 8
                Repeater {
                    model: QuickCommands.base
                    DarkButton {
                        required property string modelData
                        text: QuickCommands.label(modelData)
                        enabled: !chat.busy || modelData.indexOf("launch:") === 0
                        onClicked: bar.runAction(modelData, text)
                    }
                }
                // Learned from what the user does at this hour.
                Repeater {
                    model: bar.suggestions
                    DarkButton {
                        required property var modelData
                        text: (modelData.kind === "app" ? "▶ " : "★ ") + QuickCommands.label(modelData.id, modelData.label)
                        primary: true
                        enabled: !chat.busy || modelData.id.indexOf("ask:") !== 0
                        onClicked: bar.runAction(modelData.id, modelData.label)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Suggested from your habits")
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.bg; radius: 14
                ScrollView {
                    anchors.fill: parent; anchors.margins: 16; clip: true
                    TextArea {
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        color: Theme.text; font.pixelSize: 15; background: null
                        text: chat.busy ? qsTr("Jarvis is thinking…") : actionStatus || chat.lastReply
                              || qsTr("Type a question and press Enter.\n/web query — search the internet.\nEsc — hide this window.")
                    }
                }
            }
            RowLayout {
                Label { text: voice.busy ? voice.status : qsTr("Meta+J · quick access"); color: Theme.textDim; Layout.fillWidth: true; elide: Text.ElideRight }
                DarkButton { text: qsTr("Search the web"); enabled: query.text.trim().length>0; onClicked: bar.webSearch(query.text) }
            }
        }
    }
    Connections {
        target: voice
        function onTextReady(text) { if(bar.visible) { query.text=text; query.forceActiveFocus() } }
    }
}
