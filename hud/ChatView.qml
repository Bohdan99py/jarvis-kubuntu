import QtQuick
import Jarvis
import QtQuick.Controls

// The conversation: holographic message cards, the answer being written with
// what Jarvis is doing under it, a smooth glide to the newest message (unless
// the user scrolled up to read — then a "new" marker appears instead).
// Used by the main window and by the expanded overlay: one ChatModel, one
// conversation, wherever it is shown.
Item {
    id: view
    required property var chat
    property var vs: null
    property bool effects: true
    property alias list: list

    function scrollToEnd(animated) {
        if (list.count === 0)
            return
        const from = list.contentY
        list.positionViewAtEnd()
        const to = list.contentY
        if (!animated || !view.effects || Math.abs(to - from) < 2 || Math.abs(to - from) > list.height * 3)
            return
        list.contentY = from
        glide.to = to
        glide.restart()
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        spacing: 10
        topMargin: 12
        bottomMargin: 12
        boundsBehavior: Flickable.StopAtBounds
        model: view.chat
        // Messages that were there before the view appeared do not unfold.
        property int shownAtStart: -1
        Component.onCompleted: shownAtStart = count
        // A streaming answer grows in place: follow it unless the user scrolled up to read.
        property bool stickToEnd: true
        property bool unseen: false
        onMovementEnded: { stickToEnd = atYEnd; if (atYEnd) unseen = false }
        onCountChanged: {
            if (stickToEnd || view.chat.busy) {
                stickToEnd = true
                Qt.callLater(view.scrollToEnd, true)
            } else {
                unseen = true
            }
        }
        onContentHeightChanged: if (stickToEnd && !moving && !glide.running) Qt.callLater(list.positionViewAtEnd)
        NumberAnimation { id: glide; target: list; property: "contentY"; duration: 280; easing.type: Easing.OutCubic }

        ScrollBar.vertical: ScrollBar {
            contentItem: Rectangle { implicitWidth: 3; radius: 1; color: Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.45) }
        }

        delegate: ChatMessage {
            chat: view.chat
            vs: view.vs
            effects: view.effects
            animateIn: list.shownAtStart >= 0 && index >= list.shownAtStart
            rateable: kind === "jarvis" && index > 0 && index === ListView.view.count - 1 && !view.chat.busy
            retryable: kind === "error" && index === ListView.view.count - 1 && view.chat.canRetry
        }

        // What Jarvis is doing while no answer text has arrived yet.
        footer: Item {
            width: list.width
            height: view.chat.busy && !(view.vs && view.vs.streaming) ? 34 : 0
            visible: height > 0
            Row {
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Row {
                    spacing: 2
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: 4
                        Rectangle {
                            id: bar
                            required property int index
                            width: 3; height: 14
                            anchors.verticalCenter: parent.verticalCenter
                            color: JarvisTheme.cyan
                            transformOrigin: Item.Center
                            scale: 0.3
                            SequentialAnimation on scale {
                                running: view.chat.busy && view.visible
                                loops: Animation.Infinite
                                PauseAnimation { duration: bar.index * 110 }
                                NumberAnimation { to: 1; duration: 240; easing.type: Easing.OutQuad }
                                NumberAnimation { to: 0.3; duration: 240; easing.type: Easing.InQuad }
                                PauseAnimation { duration: (3 - bar.index) * 110 }
                            }
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: view.chat.stopping ? qsTr("stopping…")
                        : view.vs && view.vs.routes.length > 0 ? qsTr("working: %1").arg(view.vs.routes.map(JarvisTheme.routeName).join(" › "))
                        : qsTr("thinking…")
                    color: JarvisTheme.textDim
                    font.family: JarvisTheme.mono
                    font.pixelSize: 11
                    font.letterSpacing: 1
                }
            }
        }
    }

    // New messages below while the user reads above.
    Rectangle {
        visible: list.unseen && !list.atYEnd
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        width: newText.implicitWidth + 24
        height: 26
        radius: 3
        color: Qt.rgba(0.03, 0.08, 0.14, 0.92)
        border.color: JarvisTheme.cyan
        Text { id: newText; anchors.centerIn: parent; text: qsTr("↓ new messages"); color: JarvisTheme.cyan; font.family: JarvisTheme.mono; font.pixelSize: 11 }
        TapHandler { onTapped: { list.stickToEnd = true; list.unseen = false; view.scrollToEnd(true) } }
    }
}
