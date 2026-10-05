import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

ApplicationWindow {
    id: root

    width: 520
    height: 760
    minimumWidth: 360
    minimumHeight: 480
    visible: true
    title: qsTr("J.A.R.V.I.S.")
    color: theme.bg

    QtObject {
        id: theme
        readonly property color bg: "#0b0f14"
        readonly property color panel: "#121820"
        readonly property color border: "#263140"
        readonly property color text: "#e6edf3"
        readonly property color textDim: "#8b98a8"
        readonly property color accent: "#22d3ee"
        readonly property color userBubble: "#1f6feb"
        readonly property color botBubble: "#1c2530"
        readonly property int fontSize: 15
    }

    ChatModel {
        id: chat
    }

    function submit() {
        const t = input.text.trim()
        if (t.length === 0 || chat.busy)
            return
        chat.send(t)
        input.text = ""
    }

    Connections {
        target: chat
        function onBusyChanged() {
            if (!chat.busy)
                input.forceActiveFocus()
        }
    }

    Component.onCompleted: input.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- Header ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: theme.panel

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    id: core
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 7
                    color: theme.accent

                    SequentialAnimation on opacity {
                        running: true
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.35; duration: chat.busy ? 350 : 1400; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: chat.busy ? 350 : 1400; easing.type: Easing.InOutSine }
                    }
                }

                ColumnLayout {
                    spacing: 0

                    Label {
                        text: "J.A.R.V.I.S."
                        color: theme.text
                        font.pixelSize: 17
                        font.bold: true
                        font.letterSpacing: 3
                    }
                    Label {
                        text: chat.busy ? qsTr("думает…")
                              : (chat.daemonConnected ? qsTr("jarvisd подключён") : qsTr("локальный режим"))
                        color: theme.textDim
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: clearBtn
                    flat: true
                    text: qsTr("Очистить")
                    onClicked: chat.clear()

                    contentItem: Text {
                        text: clearBtn.text
                        color: clearBtn.hovered ? theme.text : theme.textDim
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 8
                        color: clearBtn.down ? theme.border : (clearBtn.hovered ? theme.botBubble : "transparent")
                    }
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
            model: chat

            ScrollBar.vertical: ScrollBar {}

            onCountChanged: Qt.callLater(list.positionViewAtEnd)

            delegate: Item {
                id: row

                required property string text
                required property bool fromUser
                required property string time

                readonly property real maxBubble: width * 0.78

                width: ListView.view.width
                height: bubble.height

                // Measures the natural single-line width so short messages get narrow bubbles.
                TextMetrics {
                    id: metrics
                    font.pixelSize: theme.fontSize
                    text: row.text
                }

                Rectangle {
                    id: bubble
                    x: row.fromUser ? row.width - width - 16 : 16
                    width: Math.min(Math.max(metrics.advanceWidth, stamp.contentWidth) + 28, row.maxBubble)
                    height: body.contentHeight + stamp.contentHeight + 22
                    radius: 14
                    color: row.fromUser ? theme.userBubble : theme.botBubble

                    NumberAnimation on opacity {
                        from: 0
                        to: 1
                        duration: 180
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
                        color: theme.text
                        font.pixelSize: theme.fontSize
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
            Layout.preferredHeight: chat.busy ? 24 : 0
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
                        color: theme.accent
                        opacity: 0.2

                        SequentialAnimation on opacity {
                            running: chat.busy
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

        // ---------- Input ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            color: theme.panel

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                TextField {
                    id: input
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    placeholderText: qsTr("Напиши сообщение…")
                    placeholderTextColor: theme.textDim
                    color: theme.text
                    font.pixelSize: theme.fontSize
                    leftPadding: 16
                    rightPadding: 16
                    selectByMouse: true
                    onAccepted: root.submit()

                    background: Rectangle {
                        radius: 20
                        color: theme.bg
                        border.width: 1
                        border.color: input.activeFocus ? theme.accent : theme.border
                    }
                }

                Button {
                    id: sendBtn
                    Layout.fillHeight: true
                    text: qsTr("Отправить")
                    enabled: !chat.busy && input.text.trim().length > 0
                    onClicked: root.submit()

                    contentItem: Text {
                        text: sendBtn.text
                        color: sendBtn.enabled ? "white" : theme.textDim
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitWidth: 104
                        implicitHeight: 40
                        radius: 20
                        color: !sendBtn.enabled ? theme.border
                               : sendBtn.down ? Qt.darker(theme.userBubble, 1.3)
                               : sendBtn.hovered ? Qt.lighter(theme.userBubble, 1.15)
                               : theme.userBubble
                    }
                }
            }
        }
    }
}
