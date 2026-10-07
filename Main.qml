import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis
import Qt.labs.platform as Platform

ApplicationWindow {
    id: root

    width: 1180
    height: 820
    minimumWidth: 1000
    minimumHeight: 700
    visible: !startQuick
    title: qsTr("J.A.R.V.I.S.")
    color: Theme.bg

    ChatModel {
        id: chatModel
    }

    VoiceController { id: voiceEngine; language: appSettings.uiLanguage }
    QuickBar { id: quickBar; objectName: "quickBar"; chat: chatModel; voice: voiceEngine; desktop: desktopBridge; settings: appSettings; onVoiceSetupRequested: { quickBar.hide(); root.show(); controlCenter.showVoice() } }
    ControlCenter { id: controlCenter; objectName: "controlCenter"; desktop: desktopBridge; voice: voiceEngine; chat: chatModel; settings: appSettings; onApiSettingsRequested: settingsDialog.open() }
    Platform.SystemTrayIcon {
        Component.onCompleted: visible = true
        icon.source: "qrc:/icons/org.jarvis.Jarvis.svg"
        tooltip: qsTr("Jarvis — personal assistant")
        onActivated: quickBar.reveal()
        menu: Platform.Menu {
            Platform.MenuItem { text: qsTr("Quick commands"); onTriggered: quickBar.reveal() }
            Platform.MenuItem { text: qsTr("Open Jarvis"); onTriggered: { root.show(); root.raise(); root.requestActivate() } }
            Platform.MenuItem { text: qsTr("Quit"); onTriggered: Qt.quit() }
        }
    }
    Connections {
        target: desktopBridge
        function onQuickRequested() { quickBar.reveal() }
        function onMainRequested() { root.show(); root.raise(); root.requestActivate() }
    }
    Connections {
        target: voiceEngine
        function onTextReady(text) { if(!quickBar.visible) { input.text=text; input.forceActiveFocus() } }
    }
    Connections { target: chatModel; function onAssistantReply(text) { voiceEngine.speak(text) } }

    AppSettings {
        id: appSettings
        onSaved: chatModel.reloadConfig()
        onLanguageApplied: chatModel.retranslate()
    }

    SettingsDialog {
        id: settingsDialog
        settings: appSettings
    }

    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: Qt.quit()
    }

    function submit() {
        const t = input.text.trim()
        if (t.length === 0 || chatModel.busy)
            return
        chatModel.send(t)
        input.text = ""
    }

    Connections {
        target: chatModel
        function onBusyChanged() {
            if (!chatModel.busy)
                input.forceActiveFocus()
        }
    }

    Component.onCompleted: { if(startQuick) quickBar.reveal(); else input.forceActiveFocus() }

    // ---------- Main menu ----------
    Menu {
        id: mainMenu
        width: 300
        padding: 6

        background: Rectangle {
            radius: 10
            color: Theme.panel
            border.width: 1
            border.color: Theme.border
        }

        DarkMenuItem { text: qsTr("Skills, voice, memory and updates"); onTriggered: controlCenter.open() }
        DarkMenuItem { text: qsTr("Quick commands · Meta+J"); onTriggered: quickBar.reveal() }
        DarkMenuItem {
            text: qsTr("Claude API settings…")
            onTriggered: settingsDialog.open()
        }
        DarkMenuItem {
            text: qsTr("Clear chat")
            onTriggered: chatModel.clear()
        }
        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.border
            }
        }
        DarkMenuItem {
            text: qsTr("Quit")
            onTriggered: Qt.quit()
        }
        DarkMenuItem {
            text: qsTr("Quit and stop jarvisd")
            enabled: chatModel.daemonConnected
            onTriggered: {
                chatModel.quitDaemon()
                Qt.quit()
            }
        }
    }

    RowLayout {
      anchors.fill: parent
      anchors.margins: 20
      spacing: 20
      ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0

        // ---------- Header ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.panel

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 7
                    color: Theme.accent

                    SequentialAnimation on opacity {
                        running: true
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: chatModel.busy ? 350 : 1400; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: chatModel.busy ? 350 : 1400; easing.type: Easing.InOutSine }
                    }
                }

                ColumnLayout {
                    spacing: 0

                    Label {
                        text: "J.A.R.V.I.S."
                        color: Theme.text
                        font.pixelSize: 17
                        font.bold: true
                        font.letterSpacing: 3
                    }
                    Label {
                        text: chatModel.busy
                              ? qsTr("thinking…")
                              : (chatModel.daemonConnected ? qsTr("jarvisd connected") : qsTr("local mode"))
                                + (appSettings.hasKey ? qsTr(" · Claude") : "")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                DarkButton { text: "⌕"; onClicked: quickBar.reveal(); Accessible.name: qsTr("Quick commands") }
                DarkButton {
                    id: menuBtn
                    text: qsTr("☰  Menu")
                    onClicked: mainMenu.popup(menuBtn, menuBtn.width - mainMenu.width, menuBtn.height + 4)
                }
            }
        }

        // ---------- Messages ----------
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            topMargin: 12
            bottomMargin: 12
            boundsBehavior: Flickable.StopAtBounds
            model: chatModel

            ScrollBar.vertical: ScrollBar {}

            onCountChanged: Qt.callLater(list.positionViewAtEnd)

            delegate: Item {
                id: row

                required property string text
                required property bool fromUser
                required property string time
                required property int index

                readonly property real maxBubble: width * 0.78
                // 👍/👎 under Jarvis's latest answer (not the greeting).
                readonly property bool rateable: !fromUser && index > 0 && index === ListView.view.count - 1 && !chatModel.busy

                width: ListView.view.width
                height: bubble.height + (rateable ? rateRow.height + 4 : 0)

                Row {
                    id: rateRow
                    visible: row.rateable
                    x: 22
                    y: bubble.height + 4
                    spacing: 6
                    Repeater {
                        model: [{label: "👍", good: true}, {label: "👎", good: false}]
                        ToolButton {
                            required property var modelData
                            text: modelData.label
                            implicitHeight: 26
                            onClicked: chatModel.feedback(modelData.good)
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.good ? qsTr("Helpful: remember this answer") : qsTr("Wrong: tell me the right answer")
                            background: Rectangle { radius: 13; color: parent.hovered ? Theme.hover : Theme.botBubble }
                        }
                    }
                }

                // Measures the natural single-line width so short messages get narrow bubbles.
                TextMetrics {
                    id: metrics
                    font.pixelSize: Theme.fontSize
                    text: row.text
                }

                Rectangle {
                    id: bubble
                    x: row.fromUser ? row.width - width - 16 : 16
                    width: Math.min(Math.max(metrics.advanceWidth, stamp.contentWidth) + 28, row.maxBubble)
                    height: body.contentHeight + stamp.contentHeight + 22
                    radius: 18
                    color: row.fromUser ? Theme.userBubble : Theme.botBubble

                    NumberAnimation on opacity {
                        from: 0
                        to: 1
                        duration: 280
                    }

                    Text {
                        id: body
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 14
                        anchors.topMargin: 8
                        text: row.text
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                    }

                    Text {
                        id: stamp
                        anchors.top: body.bottom
                        anchors.topMargin: 2
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        text: row.time
                        color: Qt.rgba(1, 1, 1, 0.45)
                        font.pixelSize: 10
                    }
                }
            }
        }

        // ---------- Typing indicator ----------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: chatModel.busy ? 24 : 0
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 120 }
            }

            Row {
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Repeater {
                    model: 3

                    Rectangle {
                        id: dot
                        required property int index
                        width: 6
                        height: 6
                        radius: 3
                        color: Theme.accent
                        opacity: 0.2

                        SequentialAnimation on opacity {
                            running: chatModel.busy
                            loops: Animation.Infinite
                            PauseAnimation { duration: dot.index * 150 }
                            NumberAnimation { to: 1.0; duration: 300 }
                            NumberAnimation { to: 0.2; duration: 300 }
                            PauseAnimation { duration: (2 - dot.index) * 150 }
                        }
                    }
                }
            }
        }

        Label { Layout.fillWidth: true; visible: voiceEngine.busy || voiceEngine.recording; text: voiceEngine.status; color: Theme.accent; wrapMode: Text.Wrap; padding: 8 }

        // ---------- Input ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: Theme.panel

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                TextField {
                    id: input
                    maximumLength: 4096
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    placeholderText: qsTr("Write a message…")
                    placeholderTextColor: Theme.textDim
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    leftPadding: 16
                    rightPadding: 16
                    selectByMouse: true
                    onAccepted: root.submit()

                    background: Rectangle {
                        radius: 20
                        color: Theme.bg
                        border.width: 1
                        border.color: input.activeFocus ? Theme.accent : Theme.border
                    }
                }

                DarkButton {
                    text: voiceEngine.recording ? "■" : qsTr("Microphone")
                    enabled: !voiceEngine.busy || voiceEngine.recording
                    onClicked: { if(!voiceEngine.ready) controlCenter.showVoice(); else if(voiceEngine.recording) voiceEngine.stop(); else voiceEngine.listen() }
                }
                DarkButton {
                    Layout.fillHeight: true
                    text: qsTr("Send")
                    primary: true
                    enabled: !chatModel.busy && input.text.trim().length > 0
                    onClicked: root.submit()
                }
            }
        }
      }
      MemoryPanel {
        objectName: "memoryPanel"
        Layout.preferredWidth: Math.max(340, root.width * 0.38)
        Layout.fillHeight: true
        chat: chatModel
        settings: appSettings
        desktop: desktopBridge
        onPrivacyRequested: controlCenter.showMemory()
      }

    }
}
