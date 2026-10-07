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
    readonly property bool english: settings.uiLanguage === "en"
    function skillText(skill, field) { return english && skill[field + "_en"] ? skill[field + "_en"] : skill[field] }
    function report(error) { optionStatus.text = error }

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
                model: [qsTr("Skills"), qsTr("Voice"), qsTr("Memory"), qsTr("Application")]
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
                Label { text: qsTr("Turn on the topics you need. Examples work locally, richer answers come from Claude."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10
                    model: center.desktop.skills
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: skill
                        required property var modelData
                        width: ListView.view.width; height: 86; radius: 14; color: Theme.bg; border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 14
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4
                                Label { text: center.skillText(skill.modelData, "name"); color: Theme.text; font.pixelSize: 17; font.bold: true; textFormat: Text.PlainText }
                                Label { text: center.skillText(skill.modelData, "description"); color: Theme.textDim; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                            }
                            DarkButton { text: qsTr("About"); onClicked: { details.skill=skill.modelData; details.open() } }
                            Switch { checked: skill.modelData.enabled; onClicked: center.desktop.setSkillEnabled(skill.modelData.id, checked); Accessible.name: center.skillText(skill.modelData, "name") }
                        }
                    }
                }
                RowLayout {
                    DarkButton { text: qsTr("Import JSON"); onClicked: skillFile.open() }
                    TextField { id: skillUrl; Layout.fillWidth: true; placeholderText: "https://…/skill.json"; placeholderTextColor: Theme.textDim; selectByMouse: true; color: Theme.text; background: Rectangle { color: Theme.bg; radius: 8 } }
                    DarkButton { text: qsTr("Fetch"); enabled: skillUrl.text.trim().length>0; onClicked: center.desktop.fetchSkill(skillUrl.text.trim()) }
                }
                Label { text: center.desktop.skillStatus; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
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
                        hint: qsTr("Facts like your name, tools and projects, topics you talk about, corrections (\"no, the correct answer is …\") and the quick actions you use. With a Claude key, Jarvis also looks back over a finished conversation and keeps what matters. Passwords, keys and card numbers are never stored.")
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
                    Label { id: optionStatus; visible: text.length > 0; color: Theme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Try in chat: \"what do you know about me\", \"what am I doing\", \"what did I do today\", \"forget …\"."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        DarkButton { text: qsTr("Clear activity history"); danger: true; onClicked: center.chat.forget("activity") }
                        DarkButton { text: qsTr("Forget facts and topics"); danger: true; onClicked: confirmForget.open() }
                    }
                    Label { text: center.chat.memoryStatus; visible: text.length > 0; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true }
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
    Dialog {
        id: details
        property var skill: ({})
        anchors.centerIn: Overlay.overlay; width: Math.min(600, center.width-30); height: 380; modal: true
        title: center.skillText(skill, "name") || qsTr("Skill"); standardButtons: Dialog.Close
        ScrollView { anchors.fill: parent; TextArea { readOnly: true; wrapMode: TextEdit.Wrap; text: (details.skill.prompt || "") + "\n\n" + qsTr("Local examples: %1").arg((details.skill.examples || []).length) } }
    }
    Dialog {
        id: confirmForget
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Forget everything about you?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Facts and conversation topics will be deleted. Taught examples and activity history stay."); wrapMode: Text.Wrap; width: 360 }
        onAccepted: center.chat.forget("facts")
    }
}
