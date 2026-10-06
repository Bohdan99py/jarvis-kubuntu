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
    signal voiceSetupRequested()
    property string actionStatus: ""
    width: 760; height: 450
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: "Jarvis — быстрые команды"
    function reveal() {
        x = screen.virtualX + (screen.width - width) / 2
        y = screen.virtualY + screen.height * 0.18
        show(); raise(); requestActivate(); query.forceActiveFocus()
    }
    function submit() {
        const text=query.text.trim()
        if (!text || chat.busy) return
        if (text.startsWith("/web ")) { desktop.searchWeb(text.substring(5)); actionStatus="Поиск открыт в браузере" }
        else { chat.send(text); actionStatus="" }
        query.text=""
    }
    Shortcut { sequence: "Escape"; onActivated: bar.hide() }
    Rectangle {
        anchors.fill: parent; radius: 24; color: Theme.panel; border.color: "#36556b"; border.width: 1
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 22; spacing: 14
            RowLayout {
                Label { text: "JARVIS"; color: Theme.accent; font.letterSpacing: 4; font.bold: true }
                Label { text: "Быстрый доступ"; color: Theme.textDim }
                Item { Layout.fillWidth: true }
                DarkButton { text: "Открыть чат"; onClicked: { desktop.ShowMain(); bar.hide() } }
                DarkButton { text: "×"; onClicked: bar.hide(); Accessible.name: "Закрыть быстрый доступ" }
            }
            RowLayout {
                TextField {
                    id: query
                    Layout.fillWidth: true; Layout.preferredHeight: 56
                    font.pixelSize: 20; color: Theme.text; placeholderTextColor: Theme.textDim
                    placeholderText: "Спросить, выполнить команду или /web поиск…"
                    maximumLength: 4096; selectByMouse: true
                    background: Rectangle { color: Theme.bg; radius: 14; border.color: query.activeFocus ? Theme.accent : Theme.border }
                    onAccepted: bar.submit()
                }
                DarkButton {
                    text: voice.recording ? "■" : "Микрофон"
                    enabled: !voice.busy || voice.recording
                    onClicked: { if(!voice.ready) bar.voiceSetupRequested(); else if(voice.recording) voice.stop(); else voice.listen() }
                }
                DarkButton { text: "↵"; primary: true; enabled: !chat.busy && query.text.trim().length>0; onClicked: bar.submit() }
            }
            Flow {
                Layout.fillWidth: true; spacing: 8
                Repeater {
                    model: [ {label:"Память",question:"память"}, {label:"Процессор",question:"cpu"}, {label:"Диск",question:"диск"},
                             {label:"Файлы",action:"files"}, {label:"Терминал",action:"terminal"}, {label:"Настройки KDE",action:"settings"} ]
                    DarkButton {
                        required property var modelData
                        text: modelData.label
                        enabled: !chat.busy
                        onClicked: {
                            if (modelData.action) actionStatus=desktop.launch(modelData.action)
                            else chat.send(modelData.question)
                        }
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
                        text: chat.busy ? "Jarvis думает…" : actionStatus || chat.lastReply || "Введите вопрос и нажмите Enter.\n/web запрос — поиск в интернете.\nEsc — скрыть окно."
                    }
                }
            }
            RowLayout {
                Label { text: voice.busy ? voice.status : "Meta+J · быстрый доступ"; color: Theme.textDim; Layout.fillWidth: true; elide: Text.ElideRight }
                DarkButton { text: "Поиск в интернете"; enabled: query.text.trim().length>0; onClicked: desktop.searchWeb(query.text) }
            }
        }
    }
    Connections {
        target: voice
        function onTextReady(text) { if(bar.visible) { query.text=text; query.forceActiveFocus() } }
    }
}
