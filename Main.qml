import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis
import Qt.labs.platform as Platform
import QtQuick.Dialogs as Dialogs

ApplicationWindow {
    id: root

    width: 1180
    height: 820
    minimumWidth: 1000
    minimumHeight: 700
    visible: !startQuick && !startTray && startOpen !== "study"
    title: qsTr("J.A.R.V.I.S.")
    color: Theme.bg

    ChatModel {
        id: chatModel
        objectName: "chatModel"
        // Memory and the graph only need to follow background changes while shown.
        liveUpdates: (root.visible && root.visibility !== Window.Minimized) || quickBar.visible
    }

    VoiceController { id: voiceEngine; language: appSettings.uiLanguage }
    StudyController { id: study; language: appSettings.uiLanguage; onMemoryLearned: chatModel.refreshMemory() }
    StudyWindow { id: studyWindow; objectName: "studyWindow"; study: study; onApiSettingsRequested: { root.show(); root.raise(); settingsDialog.open() } }
    QuickBar { id: quickBar; objectName: "quickBar"; chat: chatModel; voice: voiceEngine; desktop: desktopBridge; settings: appSettings; onVoiceSetupRequested: { quickBar.hide(); root.show(); controlCenter.showVoice() } }
    ControlCenter { id: controlCenter; objectName: "controlCenter"; desktop: desktopBridge; voice: voiceEngine; chat: chatModel; settings: appSettings; onApiSettingsRequested: settingsDialog.open() }
    Platform.SystemTrayIcon {
        id: tray
        Component.onCompleted: visible = true
        icon.source: "qrc:/icons/org.jarvis.Jarvis.svg"
        tooltip: qsTr("Jarvis — personal assistant")
        onActivated: quickBar.reveal()
        menu: Platform.Menu {
            Platform.MenuItem { text: qsTr("Quick commands"); onTriggered: quickBar.reveal() }
            Platform.MenuItem { text: qsTr("Open Jarvis"); onTriggered: { root.show(); root.raise(); root.requestActivate() } }
            Platform.MenuItem { text: qsTr("French studies"); onTriggered: studyWindow.showTab(0) }
            Platform.MenuItem { text: qsTr("Quit"); onTriggered: Qt.quit() }
        }
    }
    // At login Jarvis may start before the panel: show the icon once the tray exists.
    Timer {
        property int tries: 0
        interval: 3000
        repeat: true
        running: !tray.available && tries < 60
        // `available` may not notify: check it on every tick and stop once it is there.
        onTriggered: { if (tray.available) { tray.visible = false; tray.visible = true; tries = 60 } else tries++ }
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
    Connections {
        target: chatModel
        function onAssistantReply(text) {
            voiceEngine.speak(text)
            // Asked and switched away: the answer comes as a notification.
            if (!root.userSeesChat)
                root.notifyReady("reply", "Jarvis", text, [["default", ""], ["open", qsTr("Open chat")]])
        }
        function onOpenRequested(target, payload) { root.openTarget(target, payload) }
    }

    // ---------- Notifications ----------
    readonly property bool userSeesChat: (root.visible && root.active) || quickBar.visible
    function showMain() { root.show(); root.raise(); root.requestActivate() }
    // Where a notification leads: "main", "tasks", "study", "curious" or "memory".
    function openTarget(target, payload) {
        if (target === "study") {
            studyWindow.showTab(2)
            return
        }
        showMain()
        if (target === "tasks")
            memoryPanel.showTab(1)
        else if (target === "memory")
            memoryPanel.showTab(2)
        else if (target === "curious") {
            if (payload.length > 0)
                chatModel.say(payload)
            else
                chatModel.askMeSomething()
            input.forceActiveFocus()
        }
    }
    // Something finished while Jarvis was out of sight.
    function notifyReady(key, title, body, actions) {
        if (appSettings.notifyReplies && body.length > 0)
            notifier.show(key, title, body, actions || [["default", ""]])
    }
    Connections {
        target: notifier
        function onActionInvoked(key, action) {
            if (key === "update") {
                root.showMain()
                controlCenter.showApp()
                if (action === "update")
                    desktopBridge.update()
            } else if (key === "study-job") {
                studyWindow.show(); studyWindow.raise(); studyWindow.requestActivate()
            } else if (key === "voice") {
                root.showMain()
                controlCenter.showVoice()
            } else {
                root.showMain()
                input.forceActiveFocus()
            }
        }
    }
    Connections {
        target: desktopBridge
        function onOpenRequested(target, payload) { root.openTarget(target, payload) }
        function onUpdateAvailable(version) {
            notifier.show("update", qsTr("Jarvis %1 is available").arg(version),
                          qsTr("The package is verified against GitHub's checksum before Discover opens."),
                          [["update", qsTr("Update")], ["default", ""]])
        }
    }
    // Long jobs (transcription, notes, PDF, setup): a notification when they end out of sight.
    Connections {
        target: study
        property real startedAt: 0
        function onStatusChanged() {
            if (study.busy) {
                if (startedAt === 0) startedAt = Date.now()
                return
            }
            const slow = startedAt > 0 && Date.now() - startedAt > 15000
            startedAt = 0
            if (slow && !studyWindow.active)
                root.notifyReady("study-job", qsTr("French studies"), study.status)
        }
    }
    Connections {
        target: voiceEngine
        property real startedAt: 0
        function onChanged() {
            if (voiceEngine.busy) {
                if (startedAt === 0) startedAt = Date.now()
                return
            }
            const slow = startedAt > 0 && Date.now() - startedAt > 20000
            startedAt = 0
            if (slow && !controlCenter.visible)
                root.notifyReady("voice", qsTr("Voice"), voiceEngine.status)
        }
    }

    AppSettings {
        id: appSettings
        onSaved: { chatModel.reloadConfig(); study.reloadConfig() }
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

    // Files waiting to go with the next message (URLs).
    property var pendingFiles: []
    function attach(urls) {
        const next = root.pendingFiles.slice()
        for (let i = 0; i < urls.length && next.length < 8; ++i)
            if (next.map(String).indexOf(String(urls[i])) < 0)
                next.push(urls[i])
        root.pendingFiles = next
    }
    function fileName(url) { return decodeURIComponent(String(url).split("/").pop()) }
    function submit() {
        const t = input.text.trim()
        if ((t.length === 0 && root.pendingFiles.length === 0) || chatModel.busy)
            return
        chatModel.sendWithFiles(t, root.pendingFiles)
        input.text = ""
        root.pendingFiles = []
    }
    Dialogs.FileDialog {
        id: attachDialog
        title: qsTr("Attach files")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Images, PDF, code, schematics (*.png *.jpg *.jpeg *.gif *.webp *.pdf *.kicad_sch *.kicad_pcb *.kicad_pro *.net *.sim1 *.simu *.c *.h *.cpp *.hpp *.ino *.py *.js *.qml *.json *.txt *.md *.csv *.log *.sh *.cmake *.v *.vhd *.s *.ld *.rs *.go *.cir *.ioc CMakeLists.txt Makefile)"),
                      qsTr("All files (*)")]
        onAccepted: root.attach(selectedFiles)
    }

    Connections {
        target: chatModel
        function onBusyChanged() {
            if (!chatModel.busy)
                input.forceActiveFocus()
        }
    }

    Component.onCompleted: {
        if (startOpen.length > 0) openTarget(startOpen, "")
        else if (startQuick) quickBar.reveal()
        else if (!startTray) input.forceActiveFocus()
    }
    // Closing the window keeps Jarvis running in the tray (lessons keep recording).
    onClosing: { if(desktopBridge.takeTrayHint()) tray.showMessage(qsTr("Jarvis keeps running"), qsTr("Jarvis stays in the system tray. Click the icon to open it; \"Quit\" in its menu ends it.")) }

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

        // French studies and quick commands have their own buttons in the header.
        DarkMenuItem { text: qsTr("Control center…"); onTriggered: controlCenter.open() }
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

                    // Pulses only while thinking: an endless animation makes the
                    // window redraw every frame, even when nothing else changes.
                    SequentialAnimation on opacity {
                        running: chatModel.busy
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.35; duration: 350; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 350; easing.type: Easing.InOutSine }
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

                DarkButton {
                    text: study.vocabStats.due > 0 ? qsTr("Français · %1").arg(study.vocabStats.due) : qsTr("Français")
                    onClicked: studyWindow.showTab(0)
                    ToolTip.visible: hovered
                    ToolTip.text: study.recording ? qsTr("Recording a lesson…") : qsTr("French studies: lessons, translation, flashcards")
                    Rectangle { visible: study.recording; width: 10; height: 10; radius: 5; color: Theme.danger; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2 }
                }
                DarkButton {
                    text: "⌕"
                    onClicked: quickBar.reveal()
                    Accessible.name: qsTr("Quick commands")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Quick commands · Meta+J")
                }
                DarkButton {
                    id: menuBtn
                    text: qsTr("☰  Menu")
                    onClicked: mainMenu.popup(menuBtn, menuBtn.width - mainMenu.width, menuBtn.height + 4)
                }
            }
        }

        // ---------- Messages ----------
        DropArea {
            id: fileDrop
            Layout.fillWidth: true
            Layout.fillHeight: true
            keys: ["text/uri-list"]
            onDropped: drop => { if (drop.hasUrls) { root.attach(drop.urls); drop.accept() } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 8
                z: 2
                visible: fileDrop.containsDrag
                radius: 18
                color: Qt.rgba(0.31, 0.85, 0.83, 0.10)
                border.color: Theme.accent
                border.width: 2
                Label { anchors.centerIn: parent; text: qsTr("Drop files to attach them"); color: Theme.accent; font.pixelSize: 18 }
            }
            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: 8
                topMargin: 12
                bottomMargin: 12
                boundsBehavior: Flickable.StopAtBounds
                model: chatModel

                ScrollBar.vertical: ScrollBar {}

                onCountChanged: { stickToEnd = true; Qt.callLater(list.positionViewAtEnd) }
                // A streaming answer grows in place: follow it unless the user scrolled up to read.
                property bool stickToEnd: true
                onMovementEnded: stickToEnd = atYEnd
                onContentHeightChanged: if (stickToEnd && !moving) Qt.callLater(list.positionViewAtEnd)

                delegate: Item {
                    id: row

                    required property string text
                    required property string markdown
                    required property var segments
                    required property var files
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
                    readonly property bool hasCode: !row.fromUser && row.segments.some(segment => segment.kind === "code")

                    Rectangle {
                        id: bubble
                        x: row.fromUser ? row.width - width - 16 : 16
                        width: row.hasCode ? row.maxBubble
                                           : Math.min(Math.max(metrics.advanceWidth, filesLine.implicitWidth,
                                                               stamp.contentWidth + (copyButton.visible ? copyButton.width + 16 : 0)) + 28, row.maxBubble)
                        height: content.implicitHeight + stamp.contentHeight + 22
                        radius: 18
                        color: row.fromUser ? Theme.userBubble : Theme.botBubble

                        NumberAnimation on opacity {
                            from: 0
                            to: 1
                            duration: 280
                        }

                        // Selectable text; answers render Markdown, code gets its own blocks.
                        Column {
                            id: content
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 14
                            anchors.topMargin: 8
                            spacing: 8
                            TextEdit {
                                visible: row.fromUser && row.text.length > 0
                                width: parent.width
                                text: row.fromUser ? row.text : ""
                                textFormat: TextEdit.PlainText
                                readOnly: true
                                selectByMouse: true
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.bg
                                wrapMode: TextEdit.Wrap
                                color: Theme.text
                                font.pixelSize: Theme.fontSize
                            }
                            Text {
                                id: filesLine
                                visible: row.files.length > 0
                                width: Math.min(implicitWidth, parent.width)
                                text: "📎 " + row.files.join("  ·  ")
                                color: Qt.rgba(1, 1, 1, 0.7)
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                            Repeater {
                                model: row.fromUser ? [] : row.segments
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
                                        selectionColor: Theme.accent
                                        selectedTextColor: Theme.bg
                                        wrapMode: TextEdit.Wrap
                                        color: Theme.text
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
                                            chat: chatModel
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            id: copyButton
                            visible: !row.fromUser && row.index > 0
                            anchors.verticalCenter: stamp.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            text: copied ? qsTr("Copied") : qsTr("Copy")
                            property bool copied: false
                            color: copyMouse.containsMouse ? Theme.accent : Qt.rgba(1, 1, 1, 0.45)
                            font.pixelSize: 10
                            MouseArea {
                                id: copyMouse
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { chatModel.copyText(row.text); copyButton.copied = true; copyReset.restart() }
                            }
                            Timer { id: copyReset; interval: 1500; onTriggered: copyButton.copied = false }
                        }

                        Text {
                            id: stamp
                            anchors.top: content.bottom
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

        // Files waiting to be sent.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            visible: root.pendingFiles.length > 0
            spacing: 6
            Repeater {
                model: root.pendingFiles
                Rectangle {
                    id: fileChip
                    required property var modelData
                    required property int index
                    width: chipRow.implicitWidth + 16
                    height: 28
                    radius: 14
                    color: Theme.selected
                    border.color: Theme.border
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: "📎 " + root.fileName(fileChip.modelData); color: Theme.text; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: "×"; color: removeMouse.containsMouse ? Theme.accent : Theme.textDim; font.pixelSize: 15
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                id: removeMouse
                                anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: { const next = root.pendingFiles.slice(); next.splice(fileChip.index, 1); root.pendingFiles = next }
                            }
                        }
                    }
                }
            }
        }

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

                ToolButton {
                    Layout.fillHeight: true
                    implicitWidth: 44
                    text: "📎"
                    font.pixelSize: 18
                    enabled: !chatModel.busy
                    onClicked: attachDialog.open()
                    Accessible.name: qsTr("Attach files")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Attach files: photos, PDF datasheets, code, KiCad and SimulIDE schematics. You can also drop them onto the chat.")
                    background: Rectangle { radius: 20; color: parent.hovered ? Theme.hover : Theme.bg; border.color: Theme.border }
                }

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
                    enabled: !chatModel.busy && (input.text.trim().length > 0 || root.pendingFiles.length > 0)
                    onClicked: root.submit()
                }
            }
        }
      }
      MemoryPanel {
        id: memoryPanel
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
