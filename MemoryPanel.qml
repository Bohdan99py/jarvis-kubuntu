import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// Right-hand panel: the memory graph and teaching form, what Jarvis learned
// about the user, and what the user does on the desktop.
Rectangle {
    id: panel
    required property var chat
    required property var settings
    required property var desktop
    signal privacyRequested()

    readonly property var memory: { try { return JSON.parse(chat.memory) } catch (e) { return ({}) } }
    readonly property var facts: memory.facts || []
    readonly property var topics: memory.topics || []
    readonly property var activity: memory.activity || ({})
    readonly property var today: activity.today || ({ total: 0, apps: [], categories: [] })
    readonly property var trackingState: memory.settings || ({})
    readonly property var curiosity: memory.curiosity || ({})
    readonly property var code: memory.code || ({ languages: [], projects: [], errors: [], lessons: [], lessonCount: 0, connected: 0 })

    color: Theme.panel
    radius: 22
    border.color: Theme.border

    component SectionTitle: Label {
        color: Theme.text
        font.pixelSize: 15
        font.bold: true
        Layout.topMargin: 6
    }
    component Hint: Label {
        color: Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    component Chip: Rectangle {
        property alias text: chipLabel.text
        property color tint: Theme.accent
        signal clicked()
        implicitWidth: chipLabel.implicitWidth + 22
        implicitHeight: 28
        radius: 14
        color: chipMouse.containsMouse ? Theme.hover : Theme.bg
        border.color: Qt.darker(tint, 1.6)
        Label { id: chipLabel; anchors.centerIn: parent; color: Theme.text; font.pixelSize: 12 }
        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Label { text: "NEURAL MEMORY"; color: Theme.accent; font.letterSpacing: 3; font.pixelSize: 11 }

        TabBar {
            id: tabs
            objectName: "memoryTabs"
            Layout.fillWidth: true
            background: null
            Repeater {
                model: [qsTr("Synapses"), qsTr("About me"), qsTr("Activity"), qsTr("Code")]
                TabButton {
                    id: tab
                    required property string modelData
                    text: modelData
                    contentItem: Text { text: tab.text; color: tab.checked ? Theme.accent : Theme.textDim; font.pixelSize: 13; font.bold: tab.checked; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    background: Rectangle { color: tab.checked ? Theme.selected : Theme.bg; radius: 10 }
                }
            }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ---------- Synapses ----------
            ColumnLayout {
                spacing: 10
                Label {
                    text: qsTr("Examples: %1  ·  Facts: %2  ·  Links: %3").arg(neural.network.examples || 0).arg(neural.network.facts || 0).arg((neural.network.edges || []).length)
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                SynapseGraph { id: neural; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 150; graphData: panel.chat.graph }
                // Memory fills itself; teaching by hand is optional and folded away.
                RowLayout {
                    Layout.fillWidth: true
                    Hint { text: qsTr("I learn by myself from our chats, your answers and your activity.") }
                    DarkButton { text: "↻"; onClicked: panel.chat.refreshGraph(); Accessible.name: qsTr("Refresh") }
                }
                DarkButton {
                    id: teachToggle
                    property bool open: false
                    text: (open ? "▾ " : "▸ ") + qsTr("Teach an exact answer")
                    onClicked: open = !open
                }
                ColumnLayout {
                    visible: teachToggle.open
                    Layout.fillWidth: true
                    spacing: 8
                    Hint { text: qsTr("Save a question and the right answer. You can also correct me in chat: \"no, the correct answer is …\".") }
                    TextField {
                        id: trainingQuestion
                        Layout.fillWidth: true
                        placeholderText: qsTr("Question or phrase")
                        color: Theme.text; placeholderTextColor: Theme.textDim; maximumLength: 4096
                        background: Rectangle { color: Theme.bg; radius: 10; border.color: trainingQuestion.activeFocus ? Theme.accent : Theme.border }
                    }
                    ScrollView {
                        Layout.fillWidth: true; Layout.preferredHeight: 80
                        TextArea {
                            id: trainingAnswer
                            placeholderText: qsTr("The right answer (up to 4096 characters)")
                            color: Theme.text; placeholderTextColor: Theme.textDim; wrapMode: TextEdit.Wrap
                            background: Rectangle { color: Theme.bg; radius: 10; border.color: Theme.border }
                        }
                    }
                    DarkButton {
                        text: panel.chat.learningBusy ? qsTr("Saving…") : qsTr("Create links")
                        primary: true
                        enabled: !panel.chat.learningBusy && trainingQuestion.text.trim().length > 0 && trainingAnswer.text.trim().length > 0 && trainingAnswer.text.length <= 4096
                        onClicked: panel.chat.teach(trainingQuestion.text, trainingAnswer.text)
                    }
                }
                Label { text: panel.chat.learningStatus; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12; visible: text.length > 0 }
            }

            // ---------- About me ----------
            ColumnLayout {
                spacing: 10
                Hint {
                    text: panel.trackingState.learnDialog === false
                          ? qsTr("Learning from chat is off. Only notes you add here are kept.")
                          : qsTr("Learned from our conversations. Remove anything that is wrong — Claude sees this list.")
                }
                // Curiosity: what Jarvis would like to know next.
                Rectangle {
                    visible: !!(panel.curiosity.pending || panel.curiosity.next)
                    Layout.fillWidth: true
                    implicitHeight: curiousColumn.implicitHeight + 24
                    radius: 14
                    color: Theme.bg
                    border.color: Theme.nodeTopic
                    ColumnLayout {
                        id: curiousColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        Label { text: panel.curiosity.pending ? qsTr("WAITING FOR YOUR ANSWER") : qsTr("I'M CURIOUS"); color: Theme.nodeTopic; font.pixelSize: 10; font.letterSpacing: 2 }
                        Label {
                            text: panel.curiosity.pending || panel.curiosity.next || ""
                            color: Theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText
                        }
                        DarkButton {
                            visible: !panel.curiosity.pending
                            text: qsTr("Ask me in chat")
                            enabled: !panel.chat.busy
                            onClicked: panel.chat.askMeSomething()
                        }
                    }
                }
                ListView {
                    id: factList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                    clip: true
                    spacing: 6
                    model: panel.facts
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: factRow
                        required property var modelData
                        width: ListView.view.width
                        height: factColumn.implicitHeight + 16
                        radius: 12
                        color: Theme.bg
                        border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 6
                            spacing: 8
                            ColumnLayout {
                                id: factColumn
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: Texts.slotLabel(factRow.modelData.slot) + " · " + Texts.sourceLabel(factRow.modelData.source)
                                    color: factRow.modelData.slot === "note" ? Theme.nodeTopic : Theme.nodeFact
                                    font.pixelSize: 11
                                }
                                Label {
                                    text: factRow.modelData.value
                                    color: Theme.text
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                }
                            }
                            ToolButton {
                                text: "×"
                                onClicked: panel.chat.forget(factRow.modelData.id)
                                Accessible.name: qsTr("Forget this")
                                contentItem: Text { text: "×"; color: Theme.textDim; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                background: Rectangle { radius: 12; color: parent.hovered ? Theme.hover : "transparent" }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        visible: factList.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        text: qsTr("Nothing yet. Tell me about yourself in chat, for example \"my name is …\" or \"I use Kate\".")
                    }
                }
                SectionTitle { text: qsTr("Frequent topics"); visible: panel.topics.length > 0 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: panel.topics.length > 0
                    Repeater {
                        model: panel.topics.slice(0, 14)
                        Chip { required property var modelData; text: modelData.word + " · " + modelData.count; tint: Theme.nodeTopic }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: noteField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Something to remember about you")
                        maximumLength: 300
                        color: Theme.text; placeholderTextColor: Theme.textDim
                        background: Rectangle { color: Theme.bg; radius: 10; border.color: noteField.activeFocus ? Theme.accent : Theme.border }
                        onAccepted: if (text.trim().length > 0) { panel.chat.remember(text); text = "" }
                    }
                    DarkButton { text: qsTr("Remember"); enabled: noteField.text.trim().length > 2; onClicked: { panel.chat.remember(noteField.text); noteField.text = "" } }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: panel.chat.memoryStatus; color: Theme.accent; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    DarkButton { text: qsTr("Forget all"); danger: true; enabled: panel.facts.length > 0 || panel.topics.length > 0; onClicked: forgetDialog.open() }
                }
            }

            // ---------- Activity ----------
            ScrollView {
                id: activityScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: activityScroll.availableWidth
                    spacing: 10

                    // Tracking off: explain and offer to enable.
                    Rectangle {
                        visible: !panel.settings.trackActivity
                        Layout.fillWidth: true
                        implicitHeight: offColumn.implicitHeight + 28
                        radius: 14
                        color: Theme.bg
                        border.color: Theme.border
                        ColumnLayout {
                            id: offColumn
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8
                            Label { text: qsTr("Let Jarvis see what you do"); color: Theme.text; font.bold: true; font.pixelSize: 15 }
                            Hint { text: qsTr("With tracking on, KWin tells Jarvis which application is focused. Jarvis counts time per app, learns your habits and suggests frequent actions. Data stays on this computer; window titles are kept only if you allow it.") }
                            RowLayout {
                                DarkButton { text: qsTr("Turn on tracking"); primary: true; onClicked: panel.settings.setOption("trackActivity", true) }
                                DarkButton { text: qsTr("Privacy settings…"); onClicked: panel.privacyRequested() }
                            }
                        }
                    }

                    Hint {
                        visible: panel.settings.trackActivity && text.length > 0
                        text: Texts.trackingStatus(panel.trackingState.status)
                        color: (panel.trackingState.status === "kwin" || panel.trackingState.status === "no-kwin") ? Theme.textDim : Theme.danger
                    }

                    // Now
                    Rectangle {
                        visible: panel.settings.trackActivity
                        Layout.fillWidth: true
                        implicitHeight: nowColumn.implicitHeight + 24
                        radius: 14
                        color: Theme.bg
                        border.color: panel.activity.current ? Theme.categoryColor(panel.activity.current.category) : Theme.border
                        ColumnLayout {
                            id: nowColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 2
                            Label { text: qsTr("NOW"); color: Theme.textDim; font.pixelSize: 10; font.letterSpacing: 2 }
                            Label {
                                text: panel.activity.current ? panel.activity.current.name : qsTr("No application in focus")
                                color: Theme.text; font.pixelSize: 18; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Label {
                                visible: !!panel.activity.current
                                text: panel.activity.current ? Texts.categoryName(panel.activity.current.category) + " · " + Texts.duration(panel.activity.current.seconds) : ""
                                color: panel.activity.current ? Theme.categoryColor(panel.activity.current.category) : Theme.textDim
                                font.pixelSize: 12
                            }
                            Label {
                                visible: !!(panel.activity.current && panel.activity.current.title)
                                text: panel.activity.current && panel.activity.current.title ? panel.activity.current.title : ""
                                color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true; textFormat: Text.PlainText
                            }
                        }
                    }

                    // Today
                    RowLayout {
                        visible: panel.today.apps.length > 0
                        Layout.fillWidth: true
                        SectionTitle { text: qsTr("Today") }
                        Item { Layout.fillWidth: true }
                        Label { text: Texts.duration(panel.today.total); color: Theme.textDim; font.pixelSize: 12 }
                    }
                    // Category strip
                    Row {
                        visible: panel.today.total > 0
                        Layout.fillWidth: true
                        height: 8
                        Repeater {
                            model: panel.today.categories
                            Rectangle {
                                required property var modelData
                                width: (parent ? parent.width : 0) * modelData.seconds / Math.max(1, panel.today.total)
                                height: 8
                                color: Theme.categoryColor(modelData.id)
                            }
                        }
                    }
                    Flow {
                        visible: panel.today.categories.length > 0
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: panel.today.categories
                            Row {
                                required property var modelData
                                spacing: 4
                                Rectangle { width: 8; height: 8; radius: 4; color: Theme.categoryColor(modelData.id); anchors.verticalCenter: parent.verticalCenter }
                                Label { text: Texts.categoryName(modelData.id) + " " + Texts.duration(modelData.seconds); color: Theme.textDim; font.pixelSize: 11 }
                            }
                        }
                    }
                    Repeater {
                        model: panel.today.apps
                        ColumnLayout {
                            id: appRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: appRow.modelData.name; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 13 }
                                Label { text: Texts.duration(appRow.modelData.seconds); color: Theme.textDim; font.pixelSize: 12 }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 5
                                radius: 3
                                color: Theme.border
                                Rectangle {
                                    width: parent.width * appRow.modelData.seconds / Math.max(1, panel.today.apps[0].seconds)
                                    height: parent.height
                                    radius: 3
                                    color: Theme.categoryColor(appRow.modelData.category)
                                }
                            }
                        }
                    }

                    // Typical day
                    SectionTitle { text: qsTr("Your typical day"); visible: hourBars.maxValue > 0 }
                    Item {
                        id: hourBars
                        readonly property var hours: panel.activity.hours || []
                        readonly property real maxValue: Math.max.apply(null, hours.concat([0]))
                        visible: maxValue > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 64
                        Row {
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            spacing: 2
                            Repeater {
                                model: 24
                                Item {
                                    required property int index
                                    width: (hourBars.width - 46) / 24
                                    height: parent.height
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        radius: 2
                                        height: Math.max(1, parent.height * (hourBars.hours[index] || 0) / Math.max(1, hourBars.maxValue))
                                        color: index === new Date().getHours() ? Theme.accent : Theme.nodeTopic
                                        opacity: 0.85
                                    }
                                }
                            }
                        }
                        Label { anchors.left: parent.left; anchors.bottom: parent.bottom; text: "0"; color: Theme.textDim; font.pixelSize: 10 }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "12"; color: Theme.textDim; font.pixelSize: 10 }
                        Label { anchors.right: parent.right; anchors.bottom: parent.bottom; text: "23"; color: Theme.textDim; font.pixelSize: 10 }
                    }

                    // Suggestions learned from actions and habits
                    SectionTitle { text: qsTr("Suggested for this hour"); visible: (panel.activity.suggestions || []).length > 0 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: panel.activity.suggestions || []
                            Chip {
                                required property var modelData
                                text: (modelData.kind === "app" ? "▶ " : "★ ") + QuickCommands.label(modelData.id, modelData.label)
                                tint: modelData.kind === "app" ? Theme.categoryColor(modelData.category) : Theme.accent
                                onClicked: panel.runSuggestion(modelData)
                            }
                        }
                    }

                    Label { id: suggestionStatus; visible: text.length > 0; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    // Habits: frequent switches
                    SectionTitle { text: qsTr("Frequent switches"); visible: (panel.activity.transitions || []).length > 0 }
                    Repeater {
                        model: panel.activity.transitions || []
                        Label {
                            required property var modelData
                            text: modelData.from + "  →  " + modelData.to + "   ×" + modelData.count
                            color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }

                    // Recent spans
                    SectionTitle { text: qsTr("Recently"); visible: (panel.activity.recent || []).length > 0 }
                    Repeater {
                        model: (panel.activity.recent || []).slice(0, 6)
                        Label {
                            required property var modelData
                            text: new Date(modelData.t).toLocaleTimeString(Qt.locale(), "HH:mm") + "  " + modelData.app
                                  + (modelData.title ? " — " + modelData.title : "") + " · " + Texts.duration(modelData.seconds)
                            color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText
                        }
                    }

                    RowLayout {
                        Layout.topMargin: 6
                        DarkButton { text: qsTr("Privacy…"); onClicked: panel.privacyRequested() }
                        DarkButton { text: qsTr("Clear history"); danger: true; onClicked: panel.chat.forget("activity") }
                    }
                }
            }

            // ---------- Code ----------
            ScrollView {
                id: codeScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: codeScroll.availableWidth
                    spacing: 10

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: vsColumn.implicitHeight + 24
                        radius: 14
                        color: Theme.bg
                        border.color: panel.code.connected > 0 ? Theme.accent : Theme.border
                        ColumnLayout {
                            id: vsColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6
                            Label {
                                text: panel.code.connected > 0 ? qsTr("VS Code is connected") : qsTr("Jarvis in VS Code")
                                color: panel.code.connected > 0 ? Theme.accent : Theme.text
                                font.bold: true; font.pixelSize: 15
                            }
                            Hint {
                                text: panel.code.connected > 0
                                      ? qsTr("Select code and use Jarvis from the context menu or Ctrl+Alt+J. Rate answers with 👍/👎 — good solutions become lessons.")
                                      : qsTr("The extension lets Jarvis explain and fix code, write tests and learn your languages, projects and solutions.")
                            }
                            DarkButton {
                                visible: panel.desktop.vscodeAvailable
                                text: panel.code.connected > 0 ? qsTr("Reinstall the extension") : qsTr("Install the VS Code extension")
                                primary: panel.code.connected === 0
                                onClicked: panel.desktop.installVsCodeExtension()
                            }
                            Hint { visible: !panel.desktop.vscodeAvailable; text: qsTr("VS Code was not found. Install it (for example from Discover) and come back.") }
                            Hint { visible: panel.desktop.vscodeStatus.length > 0; text: panel.desktop.vscodeStatus; color: Theme.accent }
                        }
                    }

                    SectionTitle { text: qsTr("Languages"); visible: panel.code.languages.length > 0 }
                    Repeater {
                        model: panel.code.languages
                        ColumnLayout {
                            id: langRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: langRow.modelData.name; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
                                Label { text: Texts.duration(langRow.modelData.seconds); color: Theme.textDim; font.pixelSize: 12 }
                            }
                            Rectangle {
                                Layout.fillWidth: true; height: 5; radius: 3; color: Theme.border
                                Rectangle {
                                    width: parent.width * langRow.modelData.seconds / Math.max(1, panel.code.languages[0].seconds)
                                    height: parent.height; radius: 3; color: Theme.categoryColor("coding")
                                }
                            }
                        }
                    }

                    SectionTitle { text: qsTr("Projects"); visible: panel.code.projects.length > 0 }
                    Repeater {
                        model: panel.code.projects
                        ColumnLayout {
                            id: projectRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            Label { text: projectRow.modelData.name; color: Theme.text; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: projectRow.modelData.frameworks || []
                                    Chip { required property var modelData; text: modelData; tint: Theme.nodeFact }
                                }
                            }
                        }
                    }

                    SectionTitle { text: qsTr("Errors you meet most"); visible: panel.code.errors.length > 0 }
                    Repeater {
                        model: panel.code.errors
                        Label {
                            required property var modelData
                            text: "×" + modelData.count + "  " + modelData.message
                            color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText
                        }
                    }

                    SectionTitle { text: qsTr("Lessons learned: %1").arg(panel.code.lessonCount || 0) }
                    Hint {
                        visible: (panel.code.lessons || []).length === 0
                        text: qsTr("Solutions to your problems appear here: fixes Jarvis made, answers you rated 👍 and snippets you taught. Similar problems reuse them, even offline.")
                    }
                    Repeater {
                        model: panel.code.lessons
                        Rectangle {
                            id: lessonRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: lessonColumn.implicitHeight + 16
                            radius: 12
                            color: Theme.bg
                            border.color: Theme.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 6
                                ColumnLayout {
                                    id: lessonColumn
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: (lessonRow.modelData.language || "") + " · " + Texts.lessonSource(lessonRow.modelData.source)
                                        color: Theme.nodeFact; font.pixelSize: 11
                                    }
                                    Label {
                                        text: lessonRow.modelData.problem
                                        color: Theme.text; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                                        Layout.fillWidth: true; textFormat: Text.PlainText
                                    }
                                }
                                ToolButton {
                                    text: "×"
                                    onClicked: panel.chat.forget("lesson:" + lessonRow.modelData.id)
                                    Accessible.name: qsTr("Forget this")
                                    contentItem: Text { text: "×"; color: Theme.textDim; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { radius: 12; color: parent.hovered ? Theme.hover : "transparent" }
                                }
                            }
                        }
                    }
                    DarkButton {
                        Layout.topMargin: 6
                        visible: panel.code.languages.length > 0 || (panel.code.lessonCount || 0) > 0
                        text: qsTr("Forget everything about my code")
                        danger: true
                        onClicked: panel.chat.forget("code")
                    }
                }
            }
        }
    }

    function runSuggestion(item) {
        suggestionStatus.text = QuickCommands.run(item.id, item.label, chat, desktop, settings.uiLanguage)
    }

    Dialog {
        id: forgetDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Forget everything about you?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Facts and conversation topics will be deleted. Taught examples and activity history stay."); wrapMode: Text.Wrap; width: 360 }
        onAccepted: panel.chat.forget("facts")
    }
}
