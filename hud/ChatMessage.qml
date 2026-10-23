import QtQuick
import Jarvis
import QtQuick.Controls

// One message of the chat as a holographic card: a line draws itself, the
// frame opens, then the text fades in (new messages only). User messages lean
// right in amber, Jarvis's left in cyan, errors in red; notes from Jarvis
// (what it did, what it could not do) are a thin centred line. The answer being
// written glows and shows what Jarvis is doing; an error offers "Retry".
// Cheap on purpose: no shaders, no layers, animations run once.
Item {
    id: msg

    required property string text
    required property string markdown
    required property var segments
    required property var files
    required property bool fromUser
    required property string time
    required property int index
    required property string kind
    required property bool streaming

    required property var chat
    property var vs: null                // VisualStateController: the route of the answer being written
    property bool effects: true          // false in the Low quality: no unfolding, no pulses
    property bool animateIn: false       // a message that just arrived
    property bool rateable: false        // 👍/👎 under Jarvis's latest answer
    property bool retryable: false       // "Retry" under the latest error

    readonly property bool isUser: kind === "user"
    readonly property bool isSystem: kind === "system"
    readonly property bool isError: kind === "error"
    readonly property color tint: isUser ? JarvisTheme.amber : isError ? JarvisTheme.red : JarvisTheme.cyan
    readonly property real maxCard: width * (isUser ? 0.8 : 0.9)
    readonly property bool hasCode: !isUser && segments.some(s => s.kind === "code")
    readonly property bool active: streaming || hover.hovered

    // 0 → 1: line, frame, content (see HolographicPanel).
    property real reveal: animateIn ? 0 : 1
    function ease(v) { return 1 - Math.pow(1 - Math.max(0, Math.min(1, v)), 3) }
    NumberAnimation on reveal { running: msg.animateIn; to: 1; duration: msg.effects ? 520 : 160; easing.type: Easing.Linear }
    // A short flash when a message arrives or an answer is complete.
    property real flash: 0
    NumberAnimation { id: flashAnim; target: msg; property: "flash"; from: 1; to: 0; duration: 900; easing.type: Easing.OutCubic }
    Component.onCompleted: if (animateIn && effects && !isSystem) flashAnim.start()
    onStreamingChanged: if (!streaming && effects) flashAnim.restart()

    width: ListView.view ? ListView.view.width : 400
    height: isSystem ? systemLine.height + 4
                     : (card.height + (rateable || retryable ? actions.height + 6 : 0)) * ease(Math.min(1, reveal * 1.6))

    HoverHandler { id: hover }

    // ---------- A note from Jarvis ----------
    Item {
        id: systemLine
        visible: msg.isSystem
        width: parent.width
        height: noteText.implicitHeight + 8
        opacity: msg.ease(msg.reveal)
        Rectangle { anchors.verticalCenter: parent.verticalCenter; x: 18; width: Math.max(0, (parent.width - noteText.width) / 2 - 30); height: 1; color: JarvisTheme.line }
        Rectangle { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 18; width: Math.max(0, (parent.width - noteText.width) / 2 - 30); height: 1; color: JarvisTheme.line }
        Text {
            id: noteText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width * 0.72)
            text: "◇ " + msg.text
            color: JarvisTheme.textDim
            font.family: JarvisTheme.mono
            font.pixelSize: 11
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Measures the natural single-line width so short messages get narrow cards.
    TextMetrics { id: metrics; font.pixelSize: Theme.fontSize; text: msg.text.length > 400 ? msg.text.slice(0, 400) : msg.text }

    // ---------- A message card ----------
    Item {
        id: card
        visible: !msg.isSystem
        x: msg.isUser ? msg.width - width - 14 : 14
        width: msg.hasCode || msg.streaming ? msg.maxCard
             : Math.min(Math.max(metrics.advanceWidth, filesLine.implicitWidth, header.implicitWidth, 120) + 32, msg.maxCard)
        height: content.implicitHeight + header.height + 22
        Behavior on height { enabled: msg.effects && msg.reveal >= 1; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        // The line that draws itself first.
        Rectangle {
            visible: msg.reveal < 1
            width: Math.max(4, card.width * msg.ease(msg.reveal / 0.35))
            height: 1
            x: msg.isUser ? card.width - width : 0
            color: msg.tint
        }
        Rectangle {
            id: frame
            anchors.fill: parent
            opacity: msg.ease((msg.reveal - 0.25) / 0.35)
            radius: 4
            color: msg.isUser ? Qt.rgba(msg.tint.r, msg.tint.g, msg.tint.b, 0.08)
                 : msg.isError ? Qt.rgba(msg.tint.r, msg.tint.g, msg.tint.b, 0.08)
                 : Qt.rgba(0.04, 0.10, 0.17, 0.82)
            border.width: 1
            border.color: Qt.rgba(msg.tint.r, msg.tint.g, msg.tint.b, msg.active ? 0.7 : 0.26 + 0.5 * msg.flash)
            Behavior on border.color { enabled: msg.effects; ColorAnimation { duration: 160 } }
            // Soft glow while active or right after arriving.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: 6
                color: "transparent"
                border.width: 3
                border.color: Qt.rgba(msg.tint.r, msg.tint.g, msg.tint.b, Math.max(msg.streaming ? 0.14 : 0, 0.22 * msg.flash))
            }
            // The accent bar on the speaker's side.
            Rectangle {
                width: 2
                height: parent.height - 12
                y: 6
                x: msg.isUser ? parent.width - 2 : 0
                color: msg.tint
                opacity: 0.85
            }
            // Corner brackets.
            Repeater {
                model: 2
                delegate: Item {
                    id: bracket
                    required property int index
                    readonly property bool far: index === 1
                    width: 9; height: 9
                    x: far ? frame.width - width : 0
                    y: far ? frame.height - height : 0
                    Rectangle { x: bracket.far ? bracket.width - 1 : 0; width: 1; height: bracket.height; color: msg.tint; opacity: 0.8 }
                    Rectangle { y: bracket.far ? bracket.height - 1 : 0; width: bracket.width; height: 1; color: msg.tint; opacity: 0.8 }
                }
            }
            // While the answer is written a highlight runs along the top edge.
            Rectangle {
                visible: msg.streaming && msg.effects
                y: 0
                height: 1
                width: parent.width * 0.25
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.5; color: JarvisTheme.ice }
                    GradientStop { position: 1; color: "transparent" }
                }
                NumberAnimation on x { running: msg.streaming && msg.effects && msg.visible; loops: Animation.Infinite; from: -frame.width * 0.25; to: frame.width; duration: 1400 }
            }
        }

        // Who, when, and how the answer is being made.
        Row {
            id: header
            x: 14
            y: 7
            height: 14
            spacing: 6
            opacity: msg.ease((msg.reveal - 0.45) / 0.4)
            layoutDirection: msg.isUser ? Qt.RightToLeft : Qt.LeftToRight
            anchors.right: msg.isUser ? parent.right : undefined
            anchors.rightMargin: 14
            Text {
                text: msg.isUser ? qsTr("YOU") : msg.isError ? qsTr("ERROR") : "JARVIS"
                color: msg.tint
                font.family: JarvisTheme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2
            }
            Text { text: msg.time; color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10 }
            // The route while the answer is written: Claude › web search, calculator…
            Text {
                visible: msg.streaming && !!msg.vs && msg.vs.routes.length > 0
                text: "· " + (msg.vs ? msg.vs.routes.map(JarvisTheme.routeName).join(" › ") : "")
                color: JarvisTheme.cyan
                font.family: JarvisTheme.mono; font.pixelSize: 10
            }
            Text {
                visible: !msg.isUser && !msg.streaming && msg.index > 0
                text: copyMouse.copied ? qsTr("· copied") : qsTr("· copy")
                color: copyMouse.containsMouse ? JarvisTheme.cyan : JarvisTheme.textDim
                font.family: JarvisTheme.mono; font.pixelSize: 10
                MouseArea {
                    id: copyMouse
                    property bool copied: false
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { msg.chat.copyText(msg.text); copied = true; copyReset.restart() }
                    Timer { id: copyReset; interval: 1500; onTriggered: copyMouse.copied = false }
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Copy")
            }
        }

        // Selectable text; answers render Markdown, code gets its own blocks.
        Column {
            id: content
            x: 14
            y: header.y + header.height + 6
            width: card.width - 28
            spacing: 8
            opacity: msg.ease((msg.reveal - 0.5) / 0.5)
            transform: Translate { y: (1 - msg.ease((msg.reveal - 0.5) / 0.5)) * 6 }
            TextEdit {
                visible: msg.isUser && msg.text.length > 0
                width: parent.width
                text: msg.isUser ? msg.text : ""
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                selectionColor: JarvisTheme.amber
                selectedTextColor: JarvisTheme.void_
                wrapMode: TextEdit.Wrap
                color: JarvisTheme.text
                font.pixelSize: Theme.fontSize
            }
            Text {
                id: filesLine
                visible: msg.files.length > 0
                width: Math.min(implicitWidth, parent.width)
                text: "▣ " + msg.files.join("  ·  ")
                color: JarvisTheme.textDim
                font.family: JarvisTheme.mono
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            Repeater {
                model: msg.isUser ? [] : msg.segments
                delegate: Item {
                    id: segment
                    required property var modelData
                    width: content.width
                    height: modelData.kind === "code" ? (codeLoader.item ? codeLoader.item.implicitHeight : 0) : prose.implicitHeight
                    TextEdit {
                        id: prose
                        visible: segment.modelData.kind === "text"
                        width: parent.width
                        text: visible ? segment.modelData.markdown : ""
                        textFormat: TextEdit.MarkdownText
                        readOnly: true
                        selectByMouse: true
                        selectionColor: JarvisTheme.cyan
                        selectedTextColor: JarvisTheme.void_
                        wrapMode: TextEdit.Wrap
                        color: JarvisTheme.text
                        font.pixelSize: Theme.fontSize
                        onLinkActivated: link => { if (/^https?:\/\//.test(link)) Qt.openUrlExternally(link) }
                    }
                    Loader {
                        id: codeLoader
                        active: segment.modelData.kind === "code"
                        width: parent.width
                        sourceComponent: CodeBlock {
                            width: segment.width
                            code: segment.modelData.code || ""
                            lang: segment.modelData.lang || ""
                            file: segment.modelData.file || ""
                            chat: msg.chat
                        }
                    }
                }
            }
            // The cursor of the answer being written.
            Row {
                visible: msg.streaming
                spacing: 3
                height: 10
                Repeater {
                    model: 3
                    Rectangle {
                        id: tick
                        required property int index
                        width: 6; height: 6
                        anchors.verticalCenter: parent.verticalCenter
                        color: JarvisTheme.cyan
                        opacity: 0.3
                        SequentialAnimation on opacity {
                            running: msg.streaming && msg.visible
                            loops: Animation.Infinite
                            PauseAnimation { duration: tick.index * 140 }
                            NumberAnimation { to: 1; duration: 260 }
                            NumberAnimation { to: 0.3; duration: 260 }
                            PauseAnimation { duration: (2 - tick.index) * 140 }
                        }
                    }
                }
            }
        }
    }

    // ---------- Under the card: rating or retry ----------
    Row {
        id: actions
        visible: (msg.rateable || msg.retryable) && msg.reveal >= 1
        x: 22
        y: card.height + 4
        spacing: 6
        height: 26
        Repeater {
            model: msg.rateable ? [{label: "👍", good: true}, {label: "👎", good: false}] : []
            ToolButton {
                required property var modelData
                text: modelData.label
                implicitHeight: 26
                onClicked: msg.chat.feedback(modelData.good)
                ToolTip.visible: hovered
                ToolTip.text: modelData.good ? qsTr("Helpful: remember this answer") : qsTr("Wrong: tell me the right answer")
                background: Rectangle { radius: 3; color: parent.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"; border.color: JarvisTheme.line }
            }
        }
        ToolButton {
            visible: msg.retryable
            implicitHeight: 26
            text: qsTr("↻ Retry")
            onClicked: msg.chat.retry()
            contentItem: Text { text: parent.text; color: JarvisTheme.red; font.family: JarvisTheme.mono; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; leftPadding: 6; rightPadding: 6 }
            background: Rectangle { radius: 3; color: parent.hovered ? Qt.rgba(1, 0.3, 0.35, 0.12) : "transparent"; border.color: Qt.rgba(1, 0.3, 0.35, 0.5) }
            Accessible.name: qsTr("Retry the request")
        }
    }
}
