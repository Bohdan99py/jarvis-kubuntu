import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Jarvis

Dialog {
    id: center
    required property var desktop
    required property var voice
    required property var chat
    required property var settings
    signal apiSettingsRequested()
    function showVoice() { tabs.currentIndex=1; open() }
    function showMemory() { tabs.currentIndex=2; open() }
    function showApp() { tabs.currentIndex=4; open() }
    readonly property bool english: settings.uiLanguage === "en"
    function skillText(skill, field) { return english && skill[field + "_en"] ? skill[field + "_en"] : skill[field] }
    // The last error of a switch; shown on the Memory and Autonomy tabs.
    property string optionError: ""
    function report(error) { optionError = error }

    title: qsTr("Control center")
    modal: true; anchors.centerIn: Overlay.overlay
    width: Math.min(800, Overlay.overlay.width-40)
    height: Math.min(700, Overlay.overlay.height-40)
    padding: 22
    background: Rectangle { color: Theme.panel; radius: 22; border.color: Theme.border }
    header: Label { text: center.title; color: Theme.text; font.pixelSize: 24; padding: 22 }

    // A switch with a title and an explanation underneath.
    component OptionSwitch: RowLayout {
        id: option
        property alias text: optionTitle.text
        property string hint
        property alias checked: optionSwitch.checked
        signal toggled(bool checked)
        Layout.fillWidth: true
        spacing: 12
        Switch { id: optionSwitch; enabled: option.enabled; Layout.alignment: Qt.AlignTop; onToggled: option.toggled(checked); Accessible.name: option.text }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { id: optionTitle; color: option.enabled ? Theme.text : Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: option.hint; visible: option.hint.length > 0; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }
    component DarkCombo: ComboBox {
        id: combo
        Layout.preferredWidth: 240
        contentItem: Text { leftPadding: 12; text: combo.displayText; color: Theme.text; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { implicitHeight: 38; radius: 10; color: Theme.bg; border.color: combo.activeFocus ? Theme.accent : Theme.border }
    }

    contentItem: ColumnLayout {
        spacing: 16
        TabBar {
            id: tabs; objectName: "controlTabs"; Layout.fillWidth: true; background: null; spacing: 4
            Repeater {
                model: [qsTr("Skills"), qsTr("Voice"), qsTr("Memory"), qsTr("Autonomy"), qsTr("Application")]
                TabButton {
                    id: tabButton
                    required property string modelData
                    text: modelData
                    contentItem: Text { text: tabButton.text; color: tabButton.checked ? Theme.accent : Theme.textDim; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { color: tabButton.checked ? Theme.selected : Theme.bg; radius: 8 }
                }
            }
        }
        StackLayout {
            currentIndex: tabs.currentIndex; Layout.fillWidth: true; Layout.fillHeight: true

            // ---------- Skills ----------
            ColumnLayout {
                spacing: 10
                Label { text: qsTr("Turn on the topics you need. Open a tile for its calculators, formulas and ready answers — they work without the internet."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                GridView {
                    id: skillGrid
                    objectName: "skillGrid"
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    readonly property int columns: Math.max(1, Math.floor(width / 230))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: 236
                    model: center.desktop.skills
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: tile
                        required property var modelData
                        readonly property color tint: modelData.color || Theme.accent
                        width: skillGrid.cellWidth
                        height: skillGrid.cellHeight
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 6
                            radius: 16
                            clip: true
                            color: Theme.bg
                            border.color: tileMouse.containsMouse ? tile.tint : Theme.border
                            opacity: tile.modelData.enabled ? 1 : 0.6
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            MouseArea { id: tileMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: skillPage.show(tile.modelData) }
                            Image {
                                id: art
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                anchors.margins: 1
                                height: 104
                                source: "qrc:/icons/skills/" + (tile.modelData.image || "default") + ".svg"
                                sourceSize.width: 320
                                fillMode: Image.PreserveAspectCrop
                                scale: tileMouse.containsMouse ? 1.04 : 1
                                Behavior on scale { NumberAnimation { duration: 180 } }
                            }
                            ColumnLayout {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: art.bottom; anchors.bottom: parent.bottom
                                anchors.margins: 12; anchors.topMargin: 8
                                spacing: 3
                                Label { text: center.skillText(tile.modelData, "name"); color: Theme.text; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                                Label {
                                    text: center.skillText(tile.modelData, "description")
                                    color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                                    Layout.fillWidth: true; textFormat: Text.PlainText
                                }
                                Item { Layout.fillHeight: true }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 11
                                        color: tile.tint
                                        text: {
                                            const parts = []
                                            const tools = (tile.modelData.tools || []).length, formulas = (tile.modelData.formulas || []).length
                                            if (tools > 0) parts.push(qsTr("%1 calc.").arg(tools))
                                            if (formulas > 0) parts.push(qsTr("%1 formulas").arg(formulas))
                                            parts.push(qsTr("%1 answers").arg((tile.modelData.examples || []).length))
                                            return parts.join(" · ")
                                        }
                                    }
                                    Switch {
                                        checked: tile.modelData.enabled
                                        onClicked: center.desktop.setSkillEnabled(tile.modelData.id, checked)
                                        Accessible.name: center.skillText(tile.modelData, "name")
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    DarkButton { text: qsTr("Import JSON"); onClicked: skillFile.open() }
                    TextField { id: skillUrl; Layout.fillWidth: true; placeholderText: "https://…/skill.json"; placeholderTextColor: Theme.textDim; selectByMouse: true; color: Theme.text; background: Rectangle { color: Theme.bg; radius: 8 } }
                    DarkButton { text: qsTr("Fetch"); enabled: skillUrl.text.trim().length>0; onClicked: center.desktop.fetchSkill(skillUrl.text.trim()) }
                }
                Label { text: center.desktop.skillStatus; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText; visible: text.length > 0 }
            }

            // ---------- Voice ----------
            ColumnLayout {
                spacing: 16
                Label { text: qsTr("Talk to Jarvis"); color: Theme.text; font.pixelSize: 24; font.bold: true }
                Label { text: qsTr("Press the microphone, say a phrase and press stop.\nThe recognized text appears in the input field — send it with Enter."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Switch { text: qsTr("Speak answers aloud"); checked: center.voice.enabled; onClicked: center.voice.enabled=checked; palette.windowText: Theme.text }
                RowLayout {
                    DarkButton { text: qsTr("Test voice"); enabled: !center.voice.speaking; onClicked: { center.voice.enabled=true; center.voice.speak(qsTr("Hi! I'm Jarvis. Voice output is on.")) } }
                    DarkButton { text: qsTr("Stop"); enabled: center.voice.speaking || center.voice.recording; onClicked: center.voice.stop() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                Label {
                    text: center.voice.ready ? qsTr("Local recognition is ready")
                                             : (center.voice.language === "en" ? qsTr("English recognition model") : qsTr("Russian recognition model"))
                    color: Theme.text; font.pixelSize: 18
                }
                Label { text: qsTr("Setup downloads Vosk from PyPI and a model (~45 MB) from alphacephei.com for the interface language. After that, speech is recognized on this computer. Answers are spoken in their own language."); color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap }
                DarkButton { text: center.voice.ready ? qsTr("Check voice installation") : qsTr("Install voice"); primary: true; enabled: !center.voice.busy; onClicked: center.voice.setup() }
                Label { text: center.voice.status; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                Item { Layout.fillHeight: true }
            }

            // ---------- Memory ----------
            ScrollView {
                id: memoryScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: memoryScroll.availableWidth
                    spacing: 12
                    Label { text: qsTr("Learning and privacy"); color: Theme.text; font.pixelSize: 22; font.bold: true }
                    Label { text: qsTr("Everything Jarvis learns is stored only in your home folder (~/.local/share/jarvis, owner-only files). You can review and delete it in the memory panel."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    OptionSwitch {
                        objectName: "learnDialogSwitch"
                        text: qsTr("Learn from our conversations and my actions in Jarvis")
                        hint: qsTr("Facts like your name, tools and projects, topics you talk about, corrections (\"no, the correct answer is …\") and the quick actions you use. With a Claude key, Jarvis also looks back over a finished conversation, keeps what matters and a short summary, and recalls related earlier conversations later. Passwords, keys and card numbers are never stored.")
                        checked: center.settings.learnDialog
                        onToggled: center.report(center.settings.setOption("learnDialog", checked))
                    }
                    OptionSwitch {
                        objectName: "curiositySwitch"
                        text: qsTr("Curiosity: Jarvis asks questions to get to know me")
                        hint: qsTr("Now and then — at most every ten minutes and never in the middle of a task — Jarvis asks about you, your projects or an app you use a lot, and remembers the answer. Ignore a question to skip it.")
                        enabled: center.settings.learnDialog
                        checked: center.settings.curiosity
                        onToggled: center.report(center.settings.setOption("curiosity", checked))
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    OptionSwitch {
                        objectName: "trackActivitySwitch"
                        text: qsTr("See which application I'm working in")
                        hint: qsTr("A small KWin script reports the focused window to Jarvis. Time per application, habits and suggestions are kept for 14 days. Nothing is recorded while the screen is locked or after 15 minutes without window changes.")
                        checked: center.settings.trackActivity
                        onToggled: center.report(center.settings.setOption("trackActivity", checked))
                    }
                    OptionSwitch {
                        text: qsTr("Also keep window titles")
                        hint: qsTr("Titles show the document or page (for example \"main.cpp — jarvis\"). Private browsing and password manager windows are always skipped.")
                        enabled: center.settings.trackActivity
                        checked: center.settings.trackTitles
                        onToggled: center.report(center.settings.setOption("trackTitles", checked))
                    }
                    OptionSwitch {
                        text: qsTr("Tell Claude what I'm doing")
                        hint: qsTr("Adds the focused application (and title, if kept) and today's top apps to requests sent to Anthropic, so answers fit your current work.")
                        enabled: center.settings.trackActivity
                        checked: center.settings.shareActivity
                        onToggled: center.report(center.settings.setOption("shareActivity", checked))
                    }
                    Label { text: center.optionError; visible: text.length > 0; color: Theme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Try in chat: \"what do you know about me\", \"what did we talk about\", \"what am I doing\", \"what did I do today\", \"forget …\"."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        DarkButton { text: qsTr("Clear activity history"); danger: true; onClicked: center.chat.forget("activity") }
                        DarkButton { text: qsTr("Forget facts and topics"); danger: true; onClicked: confirmForget.open() }
                    }
                    Label { text: center.chat.memoryStatus; visible: text.length > 0; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }

            // ---------- Autonomy ----------
            ScrollView {
                id: autonomyScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: autonomyScroll.availableWidth
                    spacing: 12
                    Label { text: qsTr("Autonomy and notifications"); color: Theme.text; font.pixelSize: 22; font.bold: true }
                    Label { text: qsTr("Jarvis keeps working in the background: it delivers reminders, speaks up when it has something useful to say and tidies its memory. Notifications follow Plasma's \"Do not disturb\" mode."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: qsTr("Reminders always come, even with the window closed. Try in chat: \"remind me tomorrow at 9 to call mum\", \"in 20 minutes check the build\", \"every weekday at 8:30 exercise\", \"add a task buy solder\", \"my reminders\"."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    OptionSwitch {
                        objectName: "proactiveSwitch"
                        text: qsTr("Jarvis speaks up by itself")
                        hint: qsTr("Hints in notifications, at most one every half hour and never between 23:00 and 7:00 or while you play or watch something: a break after long work, French words due for review, a curious question when you have not talked for a while.")
                        checked: center.settings.proactive
                        onToggled: center.report(center.settings.setOption("proactive", checked))
                    }
                    RowLayout {
                        Layout.leftMargin: 56
                        spacing: 12
                        enabled: center.settings.proactive && center.settings.trackActivity
                        Label { text: qsTr("Suggest a break after"); color: parent.enabled ? Theme.text : Theme.textDim }
                        DarkCombo {
                            model: [qsTr("Never"), qsTr("1 hour"), qsTr("1.5 hours"), qsTr("2 hours")]
                            currentIndex: Math.max(0, [0, 60, 90, 120].indexOf(center.settings.breakMinutes))
                            onActivated: index => center.report(center.settings.setBreakMinutes([0, 60, 90, 120][index]))
                        }
                    }
                    Label {
                        visible: !center.settings.trackActivity
                        Layout.leftMargin: 56
                        text: qsTr("Breaks need \"See which application I'm working in\" on the Memory tab: that is how Jarvis knows you are at the computer.")
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    OptionSwitch {
                        text: qsTr("Morning plan")
                        hint: qsTr("When you sit down in the morning: today's reminders, open tasks and flashcards to review.")
                        enabled: center.settings.proactive
                        checked: center.settings.morningSummary
                        onToggled: center.report(center.settings.setOption("morningSummary", checked))
                    }
                    OptionSwitch {
                        text: qsTr("Review my memory every day")
                        hint: qsTr("With a Claude key, once a day Jarvis sends the facts it keeps about you and short summaries of your conversations to Claude to merge duplicates, drop outdated facts and note your goals and habits. Facts you added yourself are never deleted.")
                        enabled: center.settings.learnDialog && center.settings.hasKey
                        checked: center.settings.deepReflection
                        onToggled: center.report(center.settings.setOption("deepReflection", checked))
                    }
                    OptionSwitch {
                        text: qsTr("Tell me when something is ready while Jarvis is hidden")
                        hint: qsTr("An answer you were waiting for, a finished transcription or PDF translation, the voice setup.")
                        checked: center.settings.notifyReplies
                        onToggled: center.report(center.settings.setOption("notifyReplies", checked))
                    }
                    Label { text: center.optionError; visible: text.length > 0; color: Theme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    DarkButton {
                        text: qsTr("Send a test notification")
                        onClicked: notifier.show("test", "Jarvis", qsTr("Notifications work. This is how reminders will look."), [["default", ""]])
                    }
                }
            }

            // ---------- Application ----------
            ScrollView {
                id: appScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: appScroll.availableWidth
                    spacing: 14
                    Label { text: "Jarvis " + center.desktop.version; color: Theme.text; font.pixelSize: 26; font.bold: true }
                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 10
                        Label { text: qsTr("Interface language"); color: Theme.text }
                        DarkCombo {
                            objectName: "languageCombo"
                            model: [qsTr("System (%1)").arg(center.settings.uiLanguage === "ru" ? "Русский" : "English"), "Русский", "English"]
                            currentIndex: ["auto", "ru", "en"].indexOf(center.settings.language)
                            onActivated: index => center.settings.setLanguage(["auto", "ru", "en"][index])
                        }
                        Label { text: qsTr("Answer language"); color: Theme.text }
                        DarkCombo {
                            model: [qsTr("Same as my message"), "Русский", "English"]
                            currentIndex: ["auto", "ru", "en"].indexOf(center.settings.replyLanguage)
                            onActivated: index => center.settings.setReplyLanguage(["auto", "ru", "en"][index])
                        }
                    }
                    OptionSwitch {
                        objectName: "autostartSwitch"
                        text: qsTr("Start with the system in the tray and keep running")
                        hint: qsTr("Jarvis starts in the system tray when you log in and comes back by itself if it crashes. Closing the window only hides it; \"Quit\" in the tray menu ends it.")
                        checked: center.desktop.autostart
                        onToggled: center.desktop.autostart = checked
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Updates: Bohdan99py/jarvis-kubuntu"); color: Theme.textDim }
                    Label { text: qsTr("The button downloads a verified package and opens Discover. Confirm the system installation, then restart the service and the application."); color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    DarkButton { text: center.desktop.updating ? qsTr("Checking and downloading…") : qsTr("Update Jarvis"); primary: true; enabled: !center.desktop.updating; onClicked: center.desktop.update() }
                    Label { text: center.desktop.updateStatus; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                    RowLayout {
                        DarkButton { text: qsTr("Restart service"); enabled: !center.chat.busy; onClicked: center.desktop.restartDaemon() }
                        DarkButton { text: qsTr("Restart Jarvis"); enabled: !center.chat.busy && !center.voice.busy && !center.desktop.updating; onClicked: center.desktop.restart() }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Meta+J — quick commands\nA KDE menu entry and a system tray icon"); color: Theme.textDim; lineHeight: 1.4 }
                    DarkButton { text: qsTr("Claude API settings"); onClicked: { center.close(); center.apiSettingsRequested() } }
                }
            }
        }
        DarkButton { Layout.alignment: Qt.AlignRight; text: qsTr("Done"); onClicked: center.close() }
    }
    FileDialog { id: skillFile; title: qsTr("Import skill"); nameFilters: [qsTr("Jarvis skills (*.json)")]; onAccepted: center.desktop.importSkill(selectedFile) }
    SkillPage {
        id: skillPage
        desktop: center.desktop
        chat: center.chat
        english: center.english
        onAskRequested: question => { skillPage.close(); center.close(); center.chat.send(question) }
    }
    Dialog {
        id: confirmForget
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Forget everything about you?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Facts, conversation summaries and topics will be deleted. Taught examples, flashcards and activity history stay."); wrapMode: Text.Wrap; width: 360 }
        onAccepted: center.chat.forget("facts")
    }
}
