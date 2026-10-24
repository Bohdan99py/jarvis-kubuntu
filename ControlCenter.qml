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
    property var screen: null       // ScreenAwareness
    property var shortcut: null     // GlobalShortcut
    signal apiSettingsRequested()
    function showVoice() { tabs.currentIndex=1; open() }
    function showMemory() { tabs.currentIndex=2; open() }
    function showAwareness() { tabs.currentIndex=4; open() }
    function showApp() { tabs.currentIndex=5; open() }
    function showDesktop() { tabs.currentIndex=6; open() }
    // Opens a skill's page, optionally on a tab ("schematics", "kicad", …).
    function showSkill(id, tab) {
        const skill = desktop.skills.find(s => s.id === id)
        if (!skill) return
        open()
        skillPage.show(skill)
        if (tab) skillPage.showTab(tab)
    }
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
                model: [qsTr("Skills"), qsTr("Voice"), qsTr("Memory"), qsTr("Autonomy"), qsTr("Awareness"), qsTr("Application"), qsTr("Desktop")]
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
                OptionSwitch {
                    text: qsTr("Let Claude search the web for current facts")
                    hint: qsTr("Traffic law that changed, racing results, calendars and regulations: Claude may run up to three web searches per answer (Anthropic's web search tool, billed per search; your organisation must allow it in the Claude Console). Off: answers come from Claude's knowledge and say when a value should be checked.")
                    enabled: center.settings.hasKey
                    checked: center.settings.webSearch
                    onToggled: center.report(center.settings.setOption("webSearch", checked))
                }
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
            ScrollView {
                id: voiceScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: voiceScroll.availableWidth
                    spacing: 12
                    Label { text: qsTr("Jarvis's voice"); color: Theme.text; font.pixelSize: 22; font.bold: true }
                    OptionSwitch {
                        text: qsTr("Speak answers aloud")
                        hint: qsTr("Code, links and tables are not read out — only what a person would say. A new answer interrupts the one still being read.")
                        checked: center.voice.enabled
                        onToggled: center.voice.enabled = checked
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label { text: qsTr("Voice engine"); color: Theme.text; Layout.preferredWidth: 140 }
                        DarkCombo {
                            id: engineCombo
                            readonly property var values: ["auto", "elevenlabs", "chatterbox", "piper", "system"]
                            Layout.preferredWidth: 330
                            model: [qsTr("Best available"), qsTr("ElevenLabs — studio voices (cloud)"), qsTr("Chatterbox — neural, on this computer"),
                                    qsTr("Piper — light neural, on this computer"), qsTr("System voice (espeak-ng)")]
                            currentIndex: Math.max(0, values.indexOf(center.voice.engine))
                            onActivated: index => center.voice.engine = values[index]
                        }
                    }
                    Label {
                        readonly property var names: ({ elevenlabs: "ElevenLabs", chatterbox: "Chatterbox", piper: "Piper", system: qsTr("the system voice") })
                        text: qsTr("Speaking now: %1. If it fails, the next ready voice takes over, so Jarvis is never silent.").arg(names[center.voice.activeEngine] || center.voice.activeEngine)
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }

                    // ElevenLabs: cloud voices with the most natural intonation.
                    Rectangle {
                        visible: center.voice.engine === "auto" || center.voice.engine === "elevenlabs"
                        Layout.fillWidth: true
                        implicitHeight: elevenColumn.implicitHeight + 28
                        radius: 16
                        color: Theme.bg
                        border.color: center.voice.activeEngine === "elevenlabs" ? Qt.darker(Theme.accent, 1.6) : Theme.border
                        ColumnLayout {
                            id: elevenColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                            spacing: 10
                            Label { text: qsTr("ElevenLabs"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                            Label {
                                text: qsTr("Studio-quality voices that sound like a real person, in Russian, English, French and dozens of other languages. The text of each answer is sent to ElevenLabs to be spoken: it needs the internet and an API key (elevenlabs.io → Developers → API keys; the free plan includes a monthly character allowance). The key is stored only on this computer, readable by you alone.")
                                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField {
                                    id: elevenKey
                                    Layout.fillWidth: true
                                    echoMode: TextInput.Password
                                    selectByMouse: true
                                    color: Theme.text
                                    placeholderTextColor: Theme.textDim
                                    placeholderText: center.voice.elevenReady ? qsTr("Key saved (%1)").arg(center.voice.elevenKeyHint) : qsTr("ElevenLabs API key")
                                    background: Rectangle { implicitHeight: 38; color: Theme.panel; radius: 10; border.color: elevenKey.activeFocus ? Theme.accent : Theme.border }
                                    Accessible.name: qsTr("ElevenLabs API key")
                                    onAccepted: { center.voice.setElevenKey(text); text = "" }
                                }
                                DarkButton { text: qsTr("Save"); primary: true; enabled: elevenKey.text.trim().length > 0; onClicked: { center.voice.setElevenKey(elevenKey.text); elevenKey.text = "" } }
                                DarkButton { text: qsTr("Remove"); danger: true; visible: center.voice.elevenReady; onClicked: center.voice.setElevenKey("") }
                            }
                            GridLayout {
                                visible: center.voice.elevenReady
                                columns: 3
                                columnSpacing: 10
                                rowSpacing: 8
                                Layout.fillWidth: true
                                Label { text: qsTr("Voice"); color: Theme.text; Layout.preferredWidth: 130 }
                                DarkCombo {
                                    Layout.fillWidth: true
                                    readonly property var options: center.voice.elevenVoices.length > 0 ? center.voice.elevenVoices
                                                                   : [{ id: center.voice.elevenVoice, name: center.voice.elevenVoice === "JBFqnCBsd6RMkjVDRZzb" ? "George" : center.voice.elevenVoice, description: "" }]
                                    model: options.map(v => v.description ? v.name + " — " + v.description : v.name)
                                    currentIndex: Math.max(0, options.findIndex(v => v.id === center.voice.elevenVoice))
                                    onActivated: index => center.voice.chooseElevenVoice(options[index].id, options[index].name)
                                }
                                DarkButton { text: center.voice.elevenLoading ? qsTr("Loading…") : qsTr("Load my voices"); enabled: !center.voice.elevenLoading; onClicked: center.voice.loadElevenVoices() }
                                Label { text: qsTr("Model"); color: Theme.text }
                                DarkCombo {
                                    Layout.fillWidth: true
                                    Layout.columnSpan: 2
                                    readonly property var values: ["eleven_multilingual_v2", "eleven_flash_v2_5", "eleven_turbo_v2_5", "eleven_v3"]
                                    model: [qsTr("Multilingual v2 — the most natural reading"), qsTr("Flash v2.5 — starts fastest, half the price"),
                                            qsTr("Turbo v2.5 — fast, good quality"), qsTr("v3 — the most expressive")]
                                    currentIndex: Math.max(0, values.indexOf(center.voice.elevenModel))
                                    onActivated: index => center.voice.elevenModel = values[index]
                                }
                                Label { text: qsTr("Stability"); color: Theme.text }
                                Slider {
                                    id: stabilitySlider
                                    Layout.fillWidth: true
                                    from: 0; to: 1; stepSize: 0.05
                                    value: center.voice.elevenStability
                                    onMoved: center.voice.elevenStability = value
                                }
                                Label { text: stabilitySlider.value < 0.35 ? qsTr("lively") : stabilitySlider.value > 0.7 ? qsTr("even") : qsTr("balanced"); color: Theme.textDim }
                            }
                        }
                    }

                    // Chatterbox: open-source neural speech that can take on a voice from a recording.
                    Rectangle {
                        visible: center.voice.engine === "auto" || center.voice.engine === "chatterbox"
                        Layout.fillWidth: true
                        implicitHeight: chatterboxColumn.implicitHeight + 28
                        radius: 16
                        color: Theme.bg
                        border.color: center.voice.activeEngine === "chatterbox" ? Qt.darker(Theme.accent, 1.6) : Theme.border
                        ColumnLayout {
                            id: chatterboxColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                            spacing: 10
                            Label { text: qsTr("Chatterbox"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                            Label {
                                text: qsTr("A neural voice by Resemble AI that runs on this computer — nothing is sent anywhere. Give it a 5–20 second recording of a voice and it speaks like that voice. Best with an NVIDIA graphics card; on the processor each sentence takes a few seconds. The download is 1–3 GB for the program and about 3 GB for the model.")
                                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Label {
                                visible: !center.voice.chatterboxReady
                                text: center.voice.hasNvidia ? qsTr("An NVIDIA graphics card was found: the GPU build of PyTorch will be installed.")
                                                             : qsTr("No NVIDIA graphics card was found: the smaller processor build of PyTorch will be installed.")
                                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            DarkButton {
                                visible: !center.voice.chatterboxReady
                                text: qsTr("Install Chatterbox")
                                primary: true
                                enabled: !center.voice.busy
                                onClicked: center.voice.installChatterbox()
                            }
                            GridLayout {
                                visible: center.voice.chatterboxReady
                                columns: 3
                                columnSpacing: 10
                                rowSpacing: 8
                                Layout.fillWidth: true
                                Label { text: qsTr("Voice sample"); color: Theme.text; Layout.preferredWidth: 130 }
                                Label {
                                    text: center.voice.chatterboxSample.length > 0 ? center.voice.chatterboxSample : qsTr("Chatterbox's own voice")
                                    color: Theme.textDim; elide: Text.ElideMiddle; Layout.fillWidth: true; textFormat: Text.PlainText
                                }
                                RowLayout {
                                    DarkButton { text: qsTr("Choose a recording…"); onClicked: sampleDialog.open() }
                                    DarkButton { text: qsTr("Remove"); visible: center.voice.chatterboxSample.length > 0; onClicked: center.voice.setChatterboxSample("") }
                                }
                                Label { text: qsTr("Expressiveness"); color: Theme.text }
                                Slider {
                                    id: expressionSlider
                                    Layout.fillWidth: true
                                    from: 0.25; to: 1.5; stepSize: 0.05
                                    value: center.voice.chatterboxExaggeration
                                    onMoved: center.voice.chatterboxExaggeration = value
                                }
                                Label { text: expressionSlider.value < 0.45 ? qsTr("calm") : expressionSlider.value > 0.8 ? qsTr("dramatic") : qsTr("natural"); color: Theme.textDim }
                                Label { text: qsTr("Runs on"); color: Theme.text }
                                DarkCombo {
                                    Layout.columnSpan: 2
                                    readonly property var values: ["auto", "cuda", "cpu"]
                                    model: [qsTr("Graphics card if there is one"), qsTr("NVIDIA graphics card (CUDA)"), qsTr("Processor")]
                                    currentIndex: Math.max(0, values.indexOf(center.voice.chatterboxDevice))
                                    onActivated: index => center.voice.chatterboxDevice = values[index]
                                }
                            }
                        }
                    }

                    // Piper: light local neural voices, one per language.
                    Rectangle {
                        visible: center.voice.engine === "auto" || center.voice.engine === "piper"
                        Layout.fillWidth: true
                        implicitHeight: naturalColumn.implicitHeight + 28
                        radius: 16
                        color: Theme.bg
                        border.color: center.voice.activeEngine === "piper" ? Qt.darker(Theme.accent, 1.6) : Theme.border
                        ColumnLayout {
                            id: naturalColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                            spacing: 10
                            Label { text: qsTr("Piper"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                            Label {
                                text: center.voice.naturalReady
                                      ? qsTr("A neural voice (Piper) speaks on this computer: no internet, nothing is sent anywhere. Choose a voice for each language and listen to it.")
                                      : qsTr("Instead of the robotic system voice, a neural voice (Piper) that sounds like a person. It runs on this computer without the internet. The download is about 200 MB for the program and ~63 MB per voice.")
                                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            Repeater {
                                model: [{ lang: "ru", label: qsTr("Russian") }, { lang: "en", label: qsTr("English") }, { lang: "fr", label: qsTr("French (studies)") }]
                                delegate: RowLayout {
                                    id: voiceRow
                                    required property var modelData
                                    readonly property var options: center.voice.voices.filter(v => v.lang === modelData.lang)
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: voiceRow.modelData.label; color: Theme.text; Layout.preferredWidth: 130; elide: Text.ElideRight }
                                    DarkCombo {
                                        Layout.fillWidth: true
                                        Layout.maximumWidth: 320
                                        model: voiceRow.options.map(v => v.name + (v.installed ? "" : "  ⤓"))
                                        currentIndex: Math.max(0, voiceRow.options.findIndex(v => v.id === center.voice.chosenVoices[voiceRow.modelData.lang]))
                                        onActivated: index => center.voice.chooseVoice(voiceRow.modelData.lang, voiceRow.options[index].id)
                                    }
                                }
                            }
                            DarkButton {
                                readonly property bool missing: center.voice.voices.some(v => !v.installed && Object.values(center.voice.chosenVoices).indexOf(v.id) >= 0)
                                visible: !center.voice.naturalReady || missing
                                text: center.voice.naturalReady ? qsTr("Download the chosen voices") : qsTr("Install the natural voice")
                                primary: true
                                enabled: !center.voice.busy
                                onClicked: center.voice.installNatural()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label { text: qsTr("Pace"); color: Theme.text; Layout.preferredWidth: 140 }
                        Slider {
                            id: rateSlider
                            Layout.fillWidth: true
                            from: 0.7; to: 1.4; stepSize: 0.05
                            value: center.voice.rate
                            onMoved: center.voice.rate = value
                        }
                        Label { text: Math.round(rateSlider.value * 100) + " %"; color: Theme.textDim; Layout.preferredWidth: 50 }
                    }
                    RowLayout {
                        spacing: 8
                        Label { text: qsTr("Listen"); color: Theme.text; Layout.preferredWidth: 140 }
                        DarkButton { text: qsTr("Russian"); onClicked: center.voice.preview("ru") }
                        DarkButton { text: qsTr("English"); onClicked: center.voice.preview("en") }
                        DarkButton { text: qsTr("French"); onClicked: center.voice.preview("fr") }
                        DarkButton { text: qsTr("Stop"); enabled: center.voice.speaking || center.voice.recording; onClicked: center.voice.stop() }
                    }
                    Label { text: center.voice.status; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; visible: text.length > 0 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Dictation"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label { text: qsTr("Press the microphone, say a phrase and press stop. The recognized text appears in the input field — send it with Enter."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label {
                        text: center.voice.ready ? qsTr("Local recognition is ready")
                                                 : (center.voice.language === "en" ? qsTr("English recognition model") : qsTr("Russian recognition model"))
                        color: Theme.text
                    }
                    Label { text: qsTr("Setup downloads Vosk from PyPI and a model (~45 MB) from alphacephei.com for the interface language. After that, speech is recognized on this computer."); color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
                    DarkButton { text: center.voice.ready ? qsTr("Check voice installation") : qsTr("Install voice"); enabled: !center.voice.busy; onClicked: center.voice.setup() }
                }
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
                    Label { text: qsTr("Reminders"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 8
                        Label { text: qsTr("\"Snooze\" button"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [5, 10, 15, 30, 60]
                            model: values.map(v => v < 60 ? qsTr("%1 minutes").arg(v) : qsTr("1 hour"))
                            currentIndex: Math.max(0, values.indexOf(center.settings.snoozeMinutes))
                            onActivated: index => center.report(center.settings.setNumber("snoozeMinutes", values[index]))
                        }
                        Label { text: qsTr("Until I press \"Done\""); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [0, 5, 10, 15, 30]
                            model: values.map(v => v === 0 ? qsTr("remind once") : qsTr("again every %1 min (3 times)").arg(v))
                            currentIndex: Math.max(0, values.indexOf(center.settings.nagMinutes))
                            onActivated: index => center.report(center.settings.setNumber("nagMinutes", values[index]))
                        }
                        Label { text: qsTr("\"In the morning\" means"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [5, 6, 7, 8, 9, 10, 11]
                            model: values.map(v => v + ":00")
                            currentIndex: Math.max(0, values.indexOf(center.settings.morningHour))
                            onActivated: index => center.report(center.settings.setNumber("morningHour", values[index]))
                        }
                        Label { text: qsTr("\"In the afternoon\" means"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [12, 13, 14, 15, 16, 17]
                            model: values.map(v => v + ":00")
                            currentIndex: Math.max(0, values.indexOf(center.settings.afternoonHour))
                            onActivated: index => center.report(center.settings.setNumber("afternoonHour", values[index]))
                        }
                        Label { text: qsTr("\"In the evening\" means"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [17, 18, 19, 20, 21, 22]
                            model: values.map(v => v + ":00")
                            currentIndex: Math.max(0, values.indexOf(center.settings.eveningHour))
                            onActivated: index => center.report(center.settings.setNumber("eveningHour", values[index]))
                        }
                    }
                    OptionSwitch {
                        text: qsTr("Sound with reminders")
                        hint: qsTr("The alarm sound of your Plasma sound theme.")
                        checked: center.settings.reminderSound
                        onToggled: center.report(center.settings.setOption("reminderSound", checked))
                    }
                    Label {
                        text: qsTr("Repetitions you can say: every day, on weekdays, on weekends, every Monday, on Mondays and Wednesdays, every 2 hours, every 3 days, every 5th of the month, every year on 20 October; and \"15 minutes before\". A reminder can be edited with ✎ in the Tasks tab or moved in chat: \"move the reminder about the call to 5 pm\".")
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
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


            // ---------- Awareness ----------
            ScrollView {
                id: awarenessScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    id: awarenessPage
                    width: awarenessScroll.availableWidth
                    spacing: 12
                    readonly property var ctx: { try { return JSON.parse(center.chat.context) } catch (e) { return {} } }
                    readonly property var stats: ctx.suggestions || {}
                    readonly property bool paused: (ctx.pausedUntil || 0) > 0
                    readonly property string level: center.settings.awarenessLevel
                    Label { text: qsTr("Cognitive awareness"); color: Theme.text; font.pixelSize: 22; font.bold: true }
                    Label { text: qsTr("Jarvis can understand what you are working on — the application, the window title, and only if you allow it, what is on the screen — to answer with context (Ctrl+J) and, rarely, to offer help. Everything is analysed on this computer; nothing about the screen is stored; observation never runs in secret."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    Label { text: qsTr("Level"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Repeater {
                        model: [["off", qsTr("Off"), qsTr("No observation at all. Ctrl+J still opens the overlay, without context.")],
                                ["manual", qsTr("Manual"), qsTr("Context only when you ask: the overlay and questions like \"what's on my screen\". No offers.")],
                                ["assistive", qsTr("Assistive"), qsTr("Also rare offers backed by strong evidence: a build error that keeps coming back, a driving theory test on the screen.")],
                                ["proactive", qsTr("Proactive"), qsTr("Also offers for electronics, documentation and studying, still limited by the rules below.")]]
                        RadioButton {
                            id: levelButton
                            required property var modelData
                            Layout.fillWidth: true
                            checked: awarenessPage.level === modelData[0]
                            onClicked: center.report(center.settings.setAwarenessLevel(modelData[0]))
                            contentItem: ColumnLayout {
                                spacing: 0
                                anchors.left: levelButton.indicator.right; anchors.leftMargin: 8; anchors.right: parent.right
                                Label { text: levelButton.modelData[1]; color: Theme.text; font.pixelSize: 15 }
                                Label { text: levelButton.modelData[2]; color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Permissions"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    OptionSwitch {
                        text: qsTr("Windows: which application I'm in and its title")
                        hint: qsTr("A small KWin script tells Jarvis the focused window (Plasma 5 and 6, X11 and Wayland). Titles stay in memory only. Private browsing and password manager windows are never read.")
                        enabled: awarenessPage.level !== "off"
                        checked: center.settings.awarenessWindow
                        onToggled: center.report(center.settings.setOption("awarenessWindow", checked))
                    }
                    OptionSwitch {
                        id: screenSwitch
                        text: qsTr("Screen: let Jarvis read the window's content")
                        hint: qsTr("A screenshot of the window you were in, read by Tesseract on this computer, only when you ask (the ◉ button in the overlay, \"what's on my screen\") or, if set below, now and then in the background. \"◉ SCREEN\" is shown whenever it happens. Screenshots are never saved.")
                        enabled: awarenessPage.level !== "off"
                        checked: center.settings.awarenessScreen
                        onToggled: {
                            if (checked) screenConsent.open()
                            else center.report(center.settings.setOption("awarenessScreen", false))
                        }
                    }
                    OptionSwitch {
                        text: qsTr("Send screenshots to Claude")
                        hint: qsTr("With this on, a question about the screen also carries the picture to Anthropic (scaled down) — better for drawings, schematics and road signs. Off: only the recognised text goes.")
                        enabled: center.settings.awarenessScreen && awarenessPage.level !== "off"
                        checked: center.settings.awarenessCloudImages
                        onToggled: center.report(center.settings.setOption("awarenessCloudImages", checked))
                    }
                    RowLayout {
                        Layout.leftMargin: 56
                        spacing: 12
                        enabled: center.settings.awarenessScreen && (awarenessPage.level === "assistive" || awarenessPage.level === "proactive")
                        Label { text: qsTr("Background screen analysis"); color: parent.enabled ? Theme.text : Theme.textDim }
                        DarkCombo {
                            readonly property var values: [0, 2, 5, 10, 15]
                            model: values.map(v => v === 0 ? qsTr("never (only when I ask)") : qsTr("at most every %1 min").arg(v))
                            currentIndex: Math.max(0, values.indexOf(center.settings.screenIntervalMinutes))
                            onActivated: index => center.report(center.settings.setNumber("screenIntervalMinutes", values[index]))
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: !!center.screen
                        text: center.screen ? (center.screen.ocrAvailable ? qsTr("Text recognition: Tesseract (%1).").arg(center.screen.ocrLanguages)
                                                                          : qsTr("Text recognition needs Tesseract: sudo apt install tesseract-ocr tesseract-ocr-fra tesseract-ocr-rus"))
                                              + (center.screen.backend.length > 0 ? " " + qsTr("Last screenshot through: %1.").arg(center.screen.backend === "kwin" ? "KWin" : qsTr("the desktop portal")) : "")
                                              + (center.screen.status.length > 0 ? " " + center.screen.status : "")
                                            : ""
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap
                    }
                    DarkButton {
                        text: qsTr("Test: read my screen now")
                        enabled: !!center.screen && center.settings.awarenessScreen && !center.screen.capturing
                        onClicked: center.screen.test()
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Offers of help"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label { text: qsTr("One useful offer beats ten notifications. Jarvis offers help only on evidence — a repeated error, a recognised test question, going back and forth between docs and code — never because time passed. Never at night, in Do Not Disturb, while the screen is locked, while you are talking to Jarvis, or in games and films."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 8
                        enabled: awarenessPage.level === "assistive" || awarenessPage.level === "proactive"
                        Label { text: qsTr("At most one offer every"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [20, 45, 90, 180]
                            model: values.map(v => v < 60 ? qsTr("%1 minutes").arg(v) : qsTr("%1 hours").arg(v / 60))
                            currentIndex: Math.max(0, values.indexOf(center.settings.suggestionCooldownMinutes))
                            onActivated: index => center.report(center.settings.setNumber("suggestionCooldownMinutes", values[index]))
                        }
                        Label { text: qsTr("Offers per day, at most"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: [1, 2, 4, 8]
                            model: values.map(v => String(v))
                            currentIndex: Math.max(0, values.indexOf(center.settings.suggestionsPerDay))
                            onActivated: index => center.report(center.settings.setNumber("suggestionsPerDay", values[index]))
                        }
                    }
                    Repeater {
                        model: [["build", qsTr("Build errors that keep coming back")], ["driving", qsTr("Driving theory test questions")],
                                ["electronics", qsTr("Electronics: values and calculations")], ["docs", qsTr("Technical documentation")],
                                ["study", qsTr("Studying programming")]]
                        OptionSwitch {
                            required property var modelData
                            text: modelData[1]
                            enabled: awarenessPage.level === "assistive" || awarenessPage.level === "proactive"
                            checked: center.settings.awarenessMuted.indexOf(modelData[0]) < 0
                            onToggled: {
                                const muted = center.settings.awarenessMuted.filter(c => c !== modelData[0])
                                if (!checked) muted.push(modelData[0])
                                center.report(center.settings.setList("awarenessMuted", muted))
                            }
                        }
                    }
                    OptionSwitch {
                        text: qsTr("Also in fullscreen applications")
                        hint: qsTr("Off: a fullscreen window (a game, a presentation, a film) is never interrupted.")
                        enabled: awarenessPage.level === "assistive" || awarenessPage.level === "proactive"
                        checked: center.settings.awarenessFullscreen
                        onToggled: center.report(center.settings.setOption("awarenessFullscreen", checked))
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Last 30 days: %1 offers, %2 accepted, %3 declined. A \"Not now\" silences that kind of offer for 6 hours and doubles with each refusal; \"Don't offer this\" — for a month.")
                                  .arg(awarenessPage.stats.shown || 0).arg(awarenessPage.stats.accepted || 0).arg(awarenessPage.stats.declined || 0)
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Never observe these applications"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label { text: qsTr("Desktop file names or window classes, separated by commas (for example: org.telegram.desktop, firefox). Password managers, keyrings and authentication prompts are always excluded."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: excludedField
                            Layout.fillWidth: true
                            text: center.settings.awarenessExcluded.join(", ")
                            placeholderText: "org.telegram.desktop, thunderbird"
                            placeholderTextColor: Theme.textDim
                            color: Theme.text
                            selectByMouse: true
                            background: Rectangle { color: Theme.bg; radius: 8; border.color: excludedField.activeFocus ? Theme.accent : Theme.border }
                        }
                        DarkButton { text: qsTr("Save"); onClicked: center.report(center.settings.setList("awarenessExcluded", excludedField.text.split(","))) }
                        DarkButton {
                            text: qsTr("Exclude current")
                            enabled: !!awarenessPage.ctx.appKey
                            onClicked: center.report(center.settings.setList("awarenessExcluded", center.settings.awarenessExcluded.concat([awarenessPage.ctx.appKey])))
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Pause"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label {
                        text: awarenessPage.paused ? qsTr("Paused until %1: no offers, no background screenshots.").arg(new Date(awarenessPage.ctx.pausedUntil).toLocaleString(Qt.locale(), Locale.ShortFormat))
                                                   : qsTr("Observation is running at the level above.")
                        color: awarenessPage.paused ? Theme.nodeFact : Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    RowLayout {
                        DarkButton { text: qsTr("Pause 1 hour"); onClicked: center.chat.pauseAwareness(60) }
                        DarkButton { text: qsTr("Until tomorrow"); onClicked: center.chat.pauseAwareness(-1) }
                        DarkButton { text: qsTr("Resume"); enabled: awarenessPage.paused; onClicked: center.chat.pauseAwareness(0) }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Global shortcut"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label {
                        Layout.fillWidth: true
                        text: (center.shortcut ? center.shortcut.status : "") + "\n"
                              + qsTr("The overlay opens from any application. Plasma asks once to confirm the key and keeps it in System Settings → Keyboard → Shortcuts, where it can be changed if Ctrl+J is taken (browsers use it for downloads). Without the portal: bind \"J.A.R.V.I.S. → Overlay\" (jarvis --overlay) there by hand.")
                        color: Theme.textDim; wrapMode: Text.Wrap
                    }
                    RowLayout {
                        DarkButton { text: qsTr("Bind Ctrl+J"); enabled: !!center.shortcut && !center.shortcut.bound; onClicked: center.shortcut.bind() }
                        DarkButton { text: qsTr("Change the key…"); enabled: !!center.shortcut; onClicked: center.shortcut.configure() }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: center.optionError; visible: text.length > 0; color: Theme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        DarkButton { text: qsTr("Forget observed context and offer history"); danger: true; onClicked: center.chat.forget("awareness") }
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
                        Label { text: qsTr("Interface"); color: Theme.text }
                        DarkCombo {
                            objectName: "hudCombo"
                            readonly property var values: ["cinematic", "balanced", "low", "off"]
                            model: [qsTr("Cinematic HUD: every effect"), qsTr("Balanced HUD"), qsTr("Light HUD: no particles, fewer layers"), qsTr("Classic chat without the HUD")]
                            currentIndex: values.indexOf(center.settings.hudQuality)
                            onActivated: index => center.settings.hudQuality = values[index]
                        }
                        // Which GPU and API draw the interface: measured, and a choice for hybrid laptops.
                        Label { text: qsTr("Graphics card"); color: Theme.text }
                        DarkCombo {
                            objectName: "gpuCombo"
                            readonly property var values: ["auto", "integrated", "dedicated"]
                            readonly property var cards: jarvisGraphics.gpus
                            readonly property string defaultName: cards.length > 0 ? (cards.find(c => c.boot) || cards[0]).name : ""
                            readonly property string otherName: { const o = cards.filter(c => !c.boot); return o.length > 0 ? o[0].name : "" }
                            model: [qsTr("System default"),
                                    defaultName.length > 0 ? qsTr("Integrated: %1").arg(defaultName) : qsTr("Integrated"),
                                    otherName.length > 0 ? qsTr("Dedicated: %1").arg(otherName) : qsTr("Dedicated (none found)")]
                            currentIndex: values.indexOf(jarvisGraphics.gpuChoice)
                            onActivated: index => jarvisGraphics.gpuChoice = values[index]
                        }
                        Label { text: qsTr("Graphics API"); color: Theme.text }
                        DarkCombo {
                            objectName: "graphicsApiCombo"
                            readonly property var values: ["auto", "opengl", "vulkan"]
                            model: [qsTr("Automatic (OpenGL)"), "OpenGL", "Vulkan"]
                            currentIndex: values.indexOf(jarvisGraphics.apiChoice)
                            onActivated: index => jarvisGraphics.apiChoice = values[index]
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: jarvisGraphics.software ? Theme.danger : Theme.textDim
                        text: !jarvisGraphics.ready ? qsTr("Renders: measured after the first frame.")
                            : jarvisGraphics.software ? qsTr("Renders: %1 %2 — SOFTWARE rendering on the CPU (%3). The HUD will be slow; choose Low or the classic chat, or check the graphics driver.").arg(jarvisGraphics.api).arg(jarvisGraphics.apiVersion).arg(jarvisGraphics.renderer)
                            : qsTr("Renders: %1 %2 on %3 (%4).").arg(jarvisGraphics.api).arg(jarvisGraphics.apiVersion).arg(jarvisGraphics.renderingName).arg(jarvisGraphics.renderer)
                    }
                    Label {
                        visible: text.length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.danger
                        text: [jarvisGraphics.notice].concat(jarvisGraphics.warnings).filter(t => t.length > 0).join("\n")
                    }
                    RowLayout {
                        DarkButton {
                            visible: jarvisGraphics.restartNeeded
                            text: qsTr("Restart Jarvis to apply")
                            primary: true
                            enabled: !center.chat.busy && !center.voice.busy && !center.desktop.updating
                            onClicked: center.desktop.restart()
                        }
                        DarkButton {
                            text: qsTr("Copy graphics diagnostics")
                            onClicked: jarvisGraphics.copyReport()
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: qsTr("The API, the renderer, the GPU that draws and every GPU found — also printed by \"jarvis --gpu-info\".")
                        }
                    }
                    OptionSwitch {
                        objectName: "overlaySwitch"
                        text: qsTr("Compact core above other windows")
                        hint: qsTr("A small always-on-top core that shows what Jarvis is doing. Drag it anywhere; a click opens Jarvis. Ctrl+Shift+O switches it.")
                        checked: center.settings.overlay
                        onToggled: center.settings.overlay = checked
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Updates: Bohdan99py/jarvis-kubuntu"); color: Theme.textDim }
                    Label { text: qsTr("The button downloads a verified package and opens Discover. Confirm the system installation, then restart the service and the application."); color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    DarkButton { text: center.desktop.updating ? qsTr("Checking and downloading…") : qsTr("Update Jarvis"); primary: true; enabled: !center.desktop.updating; onClicked: center.desktop.update() }
                    Label { text: center.desktop.updateStatus; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                    RowLayout {
                        DarkButton { text: qsTr("Restart service"); enabled: !center.chat.busy; onClicked: center.desktop.restartDaemon() }
                        DarkButton {
                            text: qsTr("Stop service")
                            enabled: center.chat.daemonConnected && !center.chat.busy
                            onClicked: center.chat.quitDaemon()
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            ToolTip.text: qsTr("Reminders and hints pause until Jarvis or the service starts again.")
                        }
                        DarkButton { text: qsTr("Restart Jarvis"); enabled: !center.chat.busy && !center.voice.busy && !center.desktop.updating; onClicked: center.desktop.restart() }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Meta+J — quick commands\nA KDE menu entry and a system tray icon"); color: Theme.textDim; lineHeight: 1.4 }
                    DarkButton { text: qsTr("Claude API settings"); onClicked: { center.close(); center.apiSettingsRequested() } }
                }
            }

            // ---------- Desktop integration ----------
            ScrollView {
                id: desktopScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    id: desktopPage
                    width: desktopScroll.availableWidth
                    spacing: 12
                    readonly property var levelColors: ({ ok: Theme.accent, warn: Theme.nodeFact, error: Theme.danger, off: Theme.textDim })
                    // Diagnostics run when the tab is shown, not before.
                    Connections {
                        target: tabs
                        function onCurrentIndexChanged() { if (tabs.currentIndex === 6 && center.visible) plasma.refresh() }
                    }
                    Connections {
                        target: center
                        function onOpened() { if (tabs.currentIndex === 6) plasma.refresh() }
                    }
                    Label { text: qsTr("Desktop integration"); color: Theme.text; font.pixelSize: 22; font.bold: true }
                    Label { text: qsTr("How Jarvis lives in KDE Plasma: the background service, a widget on the desktop, KRunner (Alt+Space → \"jarvis …\") and actions other desktop components may start. Nothing in Plasma changes without your confirmation, and \"Restore the standard desktop\" takes every Jarvis addition away."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    // ---- JARVIS DESKTOP (0.19.2) ----
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: shellColumn.implicitHeight + 28
                        radius: 14
                        color: Theme.bg
                        border.color: shell.mode === "jarvis" ? Theme.accent : Theme.border
                        ColumnLayout {
                            id: shellColumn
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "JARVIS DESKTOP"; color: Theme.text; font.pixelSize: 19; font.bold: true; font.letterSpacing: 2 }
                                Item { Layout.fillWidth: true }
                                Label {
                                    objectName: "desktopModeLabel"
                                    text: shell.mode === "jarvis" ? qsTr("JARVIS DESKTOP is on") : qsTr("STANDARD KDE")
                                    color: shell.mode === "jarvis" ? Theme.accent : Theme.textDim
                                    font.bold: true
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: Theme.textDim
                                text: qsTr("Your Plasma desktop in the Jarvis look: the Jarvis core on the desktop, the Jarvis Dock with your applications, holographic widgets and workspaces. KWin and plasmashell stay, every Linux application works as before. Jarvis checks Plasma, backs up the layout, shows what will change and changes it only after you confirm; the standard panels go only once the dock is verified to work, and a restore brings everything back.")
                            }
                            // The look, as the desktop will show it: the real core with the chosen theme.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 190
                                radius: 10
                                color: JarvisTheme.void_
                                border.color: JarvisTheme.line
                                clip: true
                                HudController {
                                    id: previewCtl
                                    running: center.visible && tabs.currentIndex === 6
                                    quality: center.settings.desktop.quality || "balanced"
                                    hudState: "idle"
                                    maxRate: 24
                                }
                                QtObject {
                                    id: previewVs
                                    readonly property string state: "idle"
                                    readonly property bool demo: false
                                    readonly property var routes: []
                                    readonly property var counters: ({ requests: 0, errors: 0 })
                                    readonly property bool streaming: false
                                    readonly property real streamRate: 0
                                    readonly property real lastLatency: 0
                                }
                                JarvisCore {
                                    anchors.centerIn: parent
                                    anchors.verticalCenterOffset: -14
                                    width: 150; height: 150
                                    ctl: previewCtl
                                    vs: previewVs
                                    compact: true
                                }
                                // The dock as it will look: glass, a holographic edge, the workspaces.
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 8
                                    width: dockRow.implicitWidth + 20
                                    height: Math.max(26, (center.settings.desktop.dockSize || 44) * 0.6)
                                    radius: 8
                                    color: JarvisTheme.glass
                                    border.color: JarvisTheme.line
                                    Row {
                                        id: dockRow
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Repeater {
                                            model: ["GENERAL", "STUDY", "WORK", "DEV"]
                                            Rectangle {
                                                required property string modelData
                                                required property int index
                                                width: chipText.implicitWidth + 12; height: 18; radius: 5
                                                color: "transparent"
                                                border.color: index === 0 ? JarvisTheme.amber : JarvisTheme.line
                                                Text { id: chipText; anchors.centerIn: parent; text: modelData; color: JarvisTheme.text; font.pixelSize: 9 }
                                            }
                                        }
                                        Repeater {
                                            model: 4
                                            Rectangle { width: 18; height: 18; radius: 9; color: Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.25); border.color: JarvisTheme.cyan }
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                DarkButton {
                                    objectName: "desktopCheckButton"
                                    text: shell.busy && !shell.checked ? qsTr("Checking…") : qsTr("Check compatibility")
                                    enabled: !shell.busy
                                    onClicked: shell.check()
                                }
                                DarkButton {
                                    objectName: "desktopApplyButton"
                                    text: qsTr("Turn on JARVIS DESKTOP…")
                                    primary: true
                                    enabled: !shell.busy && shell.checked && shell.compatible && shell.mode !== "jarvis"
                                    onClicked: applyConsent.open()
                                }
                                DarkButton {
                                    objectName: "desktopRestoreButton"
                                    text: qsTr("Back to standard KDE…")
                                    enabled: !shell.busy && (shell.mode === "jarvis" || shell.hasBackup)
                                    onClicked: shellRestoreConsent.open()
                                }
                            }
                            Repeater {
                                model: shell.checks
                                RowLayout {
                                    id: checkRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label { text: checkRow.modelData.ok ? "✓" : "✗"; color: checkRow.modelData.ok ? Theme.accent : Theme.danger; font.bold: true }
                                    Label { text: checkRow.modelData.name + " — " + checkRow.modelData.text; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                                }
                            }
                            Label { visible: shell.plan.length > 0; text: qsTr("What will change:"); color: Theme.text; font.bold: true }
                            Repeater {
                                model: shell.plan
                                Label { required property string modelData; text: "• " + modelData; color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                            }
                            Label { visible: shell.status.length > 0; text: shell.status; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                            Label {
                                visible: shell.hasBackup
                                text: qsTr("Restore point: %1 · from a terminal: jarvis --restore-desktop").arg(shell.backupPath)
                                color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                            }
                        }
                    }

                    Label { text: qsTr("Appearance"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 8
                        Layout.fillWidth: true
                        Label { text: qsTr("Theme"); color: Theme.text }
                        DarkCombo {
                            objectName: "desktopThemeCombo"
                            readonly property var values: ["classic", "mark7", "minimal"]
                            model: [qsTr("Classic JARVIS — blue HUD, restrained"), qsTr("Mark VII — amber, more rings, expressive"), qsTr("Minimal — calm, few effects")]
                            currentIndex: values.indexOf(center.settings.desktop.theme || "classic")
                            onActivated: index => center.report(center.settings.setDesktop("theme", values[index]))
                        }
                        Label { text: qsTr("Primary and secondary colour"); color: Theme.text }
                        RowLayout {
                            Rectangle { width: 30; height: 30; radius: 6; color: JarvisTheme.primary; border.color: Theme.border
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { colourDialog.key = "primary"; colourDialog.selectedColor = JarvisTheme.primary; colourDialog.open() } } }
                            Rectangle { width: 30; height: 30; radius: 6; color: JarvisTheme.secondary; border.color: Theme.border
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { colourDialog.key = "secondary"; colourDialog.selectedColor = JarvisTheme.secondary; colourDialog.open() } } }
                            DarkButton { text: qsTr("Theme colours"); onClicked: { center.settings.setDesktop("primary", ""); center.report(center.settings.setDesktop("secondary", "")) } }
                        }
                        Label { text: qsTr("Glow"); color: Theme.text }
                        Slider { from: 0; to: 1.5; stepSize: 0.05; value: center.settings.desktop.glow !== undefined ? center.settings.desktop.glow : 1; Layout.preferredWidth: 240; onMoved: center.settings.setDesktop("glow", value) }
                        Label { text: qsTr("Panel transparency"); color: Theme.text }
                        Slider { from: 0.95; to: 0.3; stepSize: 0.05; value: center.settings.desktop.opacity !== undefined ? center.settings.desktop.opacity : 0.72; Layout.preferredWidth: 240; onMoved: center.settings.setDesktop("opacity", value) }
                        Label { text: qsTr("Animation speed"); color: Theme.text }
                        Slider { from: 0.5; to: 1.5; stepSize: 0.05; value: center.settings.desktop.motion !== undefined ? center.settings.desktop.motion : 1; Layout.preferredWidth: 240; onMoved: center.settings.setDesktop("motion", value) }
                        Label { text: qsTr("Density"); color: Theme.text }
                        DarkCombo {
                            readonly property var values: ["compact", "normal", "comfortable"]
                            model: [qsTr("Compact"), qsTr("Normal"), qsTr("Comfortable")]
                            currentIndex: values.indexOf(center.settings.desktop.density || "normal")
                            onActivated: index => center.report(center.settings.setDesktop("density", values[index]))
                        }
                        Label { text: qsTr("Desktop quality"); color: Theme.text }
                        DarkCombo {
                            objectName: "desktopQualityCombo"
                            readonly property var values: ["low", "balanced", "cinematic"]
                            model: [qsTr("Low — fewest effects, least GPU"), qsTr("Balanced"), qsTr("Cinematic — full detail")]
                            currentIndex: values.indexOf(center.settings.desktop.quality || "balanced")
                            onActivated: index => center.report(center.settings.setDesktop("quality", values[index]))
                        }
                        Label { text: qsTr("Core size and place"); color: Theme.text }
                        RowLayout {
                            DarkCombo {
                                Layout.preferredWidth: 120
                                readonly property var values: ["small", "medium", "large"]
                                model: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
                                currentIndex: values.indexOf(center.settings.desktop.coreSize || "medium")
                                onActivated: index => { center.report(center.settings.setDesktop("coreSize", values[index])); shell.update() }
                            }
                            DarkCombo {
                                Layout.preferredWidth: 120
                                readonly property var values: ["center", "left", "right"]
                                model: [qsTr("Centre"), qsTr("Left"), qsTr("Right")]
                                currentIndex: values.indexOf(center.settings.desktop.corePosition || "center")
                                onActivated: index => { center.report(center.settings.setDesktop("corePosition", values[index])); shell.update() }
                            }
                        }
                        Label { text: qsTr("Dock"); color: Theme.text }
                        RowLayout {
                            DarkCombo {
                                Layout.preferredWidth: 120
                                readonly property var values: ["bottom", "top", "left", "right"]
                                model: [qsTr("Bottom"), qsTr("Top"), qsTr("Left"), qsTr("Right")]
                                currentIndex: values.indexOf(center.settings.desktop.dockPosition || "bottom")
                                onActivated: index => { center.report(center.settings.setDesktop("dockPosition", values[index])); shell.update() }
                            }
                            SpinBox {
                                from: 32; to: 72
                                value: center.settings.desktop.dockSize || 44
                                onValueModified: { center.settings.setDesktop("dockSize", value); shell.update() }
                            }
                            DarkCombo {
                                Layout.preferredWidth: 130
                                readonly property var values: ["center", "left", "right"]
                                model: [qsTr("Centred"), qsTr("At the start"), qsTr("At the end")]
                                currentIndex: values.indexOf(center.settings.desktop.dockAlignment || "center")
                                onActivated: index => { center.report(center.settings.setDesktop("dockAlignment", values[index])); shell.update() }
                            }
                        }
                    }
                    OptionSwitch {
                        text: qsTr("Hide the dock automatically")
                        hint: qsTr("It slides in when the pointer touches the screen edge.")
                        checked: !!center.settings.desktop.dockAutohide
                        onToggled: { center.report(center.settings.setDesktop("dockAutohide", checked)); shell.update() }
                    }
                    OptionSwitch {
                        text: qsTr("Replace the standard panels with the dock")
                        hint: qsTr("Off: the Jarvis Dock is added and your panels stay. On: they are removed after the dock is verified to work, and come back with a restore.")
                        checked: center.settings.desktop.replacePanels !== false
                        onToggled: center.report(center.settings.setDesktop("replacePanels", checked))
                    }
                    OptionSwitch {
                        text: qsTr("Effects in the desktop widgets")
                        hint: qsTr("Off: the core and the widgets stand still and only update their numbers.")
                        checked: center.settings.desktop.effects !== false
                        onToggled: center.report(center.settings.setDesktop("effects", checked))
                    }

                    Label { text: qsTr("Workspaces"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim
                        text: qsTr("Each profile has a KDE virtual desktop, the applications it starts, the files \"prepare\" opens, the widgets it shows and how active Jarvis is there. Switch here, in the dock, in KRunner (\"switch to dev\") or by asking: \"Jarvis, prepare the desktop for programming\". Windows are moved with KWin's own scripting; switching never closes an application.")
                    }
                    Repeater {
                        id: profileList
                        model: shell.profiles
                        Rectangle {
                            id: profileCard
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: profileColumn.implicitHeight + 20
                            radius: 10
                            color: Theme.bg
                            border.color: Theme.border
                            ColumnLayout {
                                id: profileColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: profileCard.modelData.label; color: Theme.text; font.bold: true; font.letterSpacing: 2 }
                                    Label { text: qsTr("desktop"); color: Theme.textDim }
                                    SpinBox {
                                        from: 1; to: 20
                                        value: profileCard.modelData.desktop
                                        onValueModified: center.report(shell.setProfile(profileCard.modelData.id, "desktop", value))
                                    }
                                    Item { Layout.fillWidth: true }
                                    DarkButton { text: qsTr("Switch"); onClicked: center.desktop.switchWorkspace(profileCard.modelData.id) }
                                    DarkButton { text: qsTr("Prepare"); onClicked: center.desktop.switchWorkspace("prepare:" + profileCard.modelData.id) }
                                }
                                // The applications this workspace starts (0.19.3): chips with their real
                                // names, × to remove, and a search of the installed ones to add.
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Repeater {
                                        model: profileCard.modelData.apps || []
                                        Rectangle {
                                            id: appChip
                                            required property int index
                                            required property string modelData
                                            readonly property var info: shell.describeApp(modelData)
                                            height: 28; radius: 14
                                            width: chipRow.implicitWidth + 16
                                            color: Theme.panel
                                            border.color: info.installed ? Theme.border : Theme.danger
                                            RowLayout {
                                                id: chipRow
                                                anchors.centerIn: parent
                                                spacing: 6
                                                Label { text: appChip.info.name + (appChip.info.installed ? "" : " · " + qsTr("not installed")); color: Theme.text }
                                                Label {
                                                    text: "×"; color: Theme.textDim; font.pixelSize: 16
                                                    TapHandler {
                                                        onTapped: {
                                                            const list = (profileCard.modelData.apps || []).filter((x, i) => i !== appChip.index)
                                                            center.report(shell.setProfile(profileCard.modelData.id, "apps", list))
                                                        }
                                                    }
                                                    Accessible.role: Accessible.Button
                                                    Accessible.name: qsTr("Remove %1").arg(appChip.info.name)
                                                }
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    TextField {
                                        id: appSearch
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("Add an application: type its name")
                                        property var found: text.trim().length >= 2 ? shell.findApps(text) : []
                                        onAccepted: if (found.length > 0) addFound(found[0].id)
                                        function addFound(id) {
                                            const list = (profileCard.modelData.apps || []).filter(x => x !== id)
                                            list.push(id)
                                            center.report(shell.setProfile(profileCard.modelData.id, "apps", list))
                                            text = ""
                                        }
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    visible: appSearch.found.length > 0
                                    Repeater {
                                        model: appSearch.found
                                        DarkButton {
                                            required property var modelData
                                            text: "+ " + modelData.name
                                            onClicked: appSearch.addFound(modelData.id)
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: qsTr("Jarvis here"); color: Theme.textDim }
                                    DarkCombo {
                                        Layout.preferredWidth: 300
                                        readonly property var values: ["normal", "reduced", "quiet"]
                                        model: [qsTr("As in Awareness settings"), qsTr("Fewer suggestions (at most assistive)"), qsTr("Quiet: no suggestions, answers only")]
                                        currentIndex: Math.max(0, values.indexOf(profileCard.modelData.activity || "normal"))
                                        onActivated: index => center.report(shell.setProfile(profileCard.modelData.id, "activity", values[index]))
                                    }
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    text: (profileCard.modelData.paths || []).join(", ")
                                    placeholderText: qsTr("Files, folders or projects that \"prepare\" opens")
                                    onEditingFinished: center.report(shell.setProfile(profileCard.modelData.id, "paths", text.split(",").map(x => x.trim()).filter(x => x.length > 0)))
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Repeater {
                                        model: [["org.jarvis.system", qsTr("System")], ["org.jarvis.network", qsTr("Network")], ["org.jarvis.power", qsTr("Power")],
                                                ["org.jarvis.media", qsTr("Media")], ["org.jarvis.status", qsTr("Status")]]
                                        CheckBox {
                                            required property var modelData
                                            text: modelData[1]
                                            checked: (profileCard.modelData.widgets || []).indexOf(modelData[0]) >= 0
                                            onToggled: {
                                                let list = (profileCard.modelData.widgets || []).filter(w => w !== modelData[0])
                                                if (checked) list.push(modelData[0])
                                                center.report(shell.setProfile(profileCard.modelData.id, "widgets", list))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        DarkButton { text: qsTr("Save the window layout of this workspace"); onClicked: center.desktop.saveWorkspaceLayout() }
                        DarkButton { text: qsTr("Create the workspaces' desktops…"); onClicked: desktopsConsent.open() }
                    }

                    Label { text: qsTr("Widgets"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim
                        text: qsTr("Add a widget to the desktop, or remove it. Move and resize them in Plasma's edit mode; Plasma keeps where they are.")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [["org.jarvis.hud", qsTr("Core")], ["org.jarvis.system", qsTr("System")], ["org.jarvis.network", qsTr("Network")],
                                    ["org.jarvis.power", qsTr("Power")], ["org.jarvis.media", qsTr("Media")], ["org.jarvis.status", qsTr("Status")]]
                            RowLayout {
                                required property var modelData
                                spacing: 2
                                DarkButton { text: "+ " + modelData[1]; enabled: plasma.plasmaRunning && !plasma.busy; onClicked: plasma.addWidget(modelData[0]) }
                                DarkButton { text: "−"; enabled: plasma.plasmaRunning && !plasma.busy; onClicked: plasma.removeWidgets(modelData[0]); ToolTip.visible: hovered; ToolTip.text: qsTr("Remove") }
                            }
                        }
                    }
                    RowLayout {
                        DarkButton { text: qsTr("Edit the layout in Plasma"); onClicked: shell.setEditMode(true) }
                        DarkButton { text: qsTr("Done editing"); onClicked: shell.setEditMode(false) }
                    }

                    Label { text: qsTr("Service"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    OptionSwitch {
                        objectName: "autostartSwitch"
                        text: qsTr("Start with the system in the tray and keep running")
                        hint: qsTr("Jarvis starts in the system tray when you log in and comes back by itself if it crashes. Closing the window only hides it; \"Quit\" in the tray menu ends it.")
                        checked: center.desktop.autostart
                        onToggled: center.desktop.autostart = checked
                    }
                    OptionSwitch {
                        objectName: "backgroundSwitch"
                        text: qsTr("Background work")
                        hint: qsTr("The service keeps reminders, hints, KRunner and the desktop widget working after the Jarvis window is closed or quit. Off: the service ends a few seconds after the window (and two minutes after login if the window does not start); reminders wait until Jarvis is open again.")
                        checked: center.settings.background
                        onToggled: center.report(center.settings.setOption("background", checked))
                    }

                    Label { text: qsTr("Plasma"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    OptionSwitch {
                        objectName: "integrationSwitch"
                        text: qsTr("Integration with Plasma")
                        hint: qsTr("KRunner answers \"jarvis <question>\", the desktop widget shows the last notification and opens the overlay, and desktop components may start the actions Jarvis lists (open the overlay, tasks, memory, pause awareness). No component can run commands or read your memory through it. Off: KRunner and the widget only say that the integration is off.")
                        checked: center.settings.desktopIntegration
                        onToggled: center.report(center.settings.setOption("desktopIntegration", checked))
                    }
                    OptionSwitch {
                        objectName: "shareActiveAppSwitch"
                        text: qsTr("Let desktop components know the active application")
                        hint: qsTr("Its name only (\"kate\", \"firefox\"), never the window title, and nothing about private or excluded windows. Needs window awareness. Any program of your session could then ask for it — leave it off unless a widget or script of yours needs it.")
                        enabled: center.settings.desktopIntegration && center.settings.awarenessWindow
                        checked: center.settings.shareActiveApp
                        onToggled: center.report(center.settings.setOption("shareActiveApp", checked))
                    }

                    Label { text: qsTr("Desktop HUD"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label {
                        text: qsTr("A Plasma widget with Jarvis's state, a small animated core, system activity and the last notification; a click opens the overlay. It rests while windows cover the desktop and calms down on battery. Plasma 6 draws it with its own components: a problem in Jarvis never reaches the rest of the desktop.")
                        color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: plasma.widgetInstalled ? Theme.text : Theme.nodeFact
                        text: !plasma.widgetInstalled ? qsTr("The widget package is not installed.")
                            : !plasma.plasmaRunning ? qsTr("Plasma is not running here.")
                            : plasma.widgetCount < 0 ? qsTr("Checking the desktop…")
                            : plasma.widgetCount === 0 ? qsTr("Not on the desktop.")
                            : qsTr("On the desktop: %1").arg(plasma.widgetCount)
                    }
                    RowLayout {
                        DarkButton {
                            objectName: "addWidgetButton"
                            text: qsTr("Add to the desktop…")
                            primary: true
                            enabled: plasma.widgetInstalled && plasma.plasmaRunning && !plasma.busy && plasma.widgetCount === 0
                            onClicked: addWidgetConsent.open()
                        }
                        DarkButton {
                            text: qsTr("Remove from the desktop")
                            enabled: plasma.plasmaRunning && !plasma.busy && plasma.widgetCount > 0
                            onClicked: removeWidgetConsent.open()
                        }
                    }
                    RowLayout {
                        spacing: 12
                        Label { text: qsTr("Animations"); color: Theme.text }
                        DarkCombo {
                            objectName: "hudAnimationCombo"
                            readonly property var values: ["off", "low", "normal", "high"]
                            model: [qsTr("Off (still)"), qsTr("Low"), qsTr("Normal"), qsTr("High")]
                            currentIndex: values.indexOf(center.settings.hudAnimation)
                            onActivated: index => center.report(center.settings.setHudAnimation(values[index]))
                        }
                    }
                    Label { text: qsTr("On battery the widget uses one step less; under maximized or fullscreen windows it stops."); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                    Label { visible: plasma.status.length > 0; text: plasma.status; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }

                    Label { text: qsTr("Service diagnostics"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Repeater {
                        model: plasma.diagnostics
                        RowLayout {
                            id: diagRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle { width: 10; height: 10; radius: 5; Layout.alignment: Qt.AlignTop; Layout.topMargin: 5; color: desktopPage.levelColors[diagRow.modelData.level] || Theme.textDim }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Label { text: diagRow.modelData.name; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label { text: diagRow.modelData.text; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                            }
                        }
                    }
                    Label { visible: plasma.diagnostics.length === 0; text: plasma.busy ? qsTr("Checking…") : qsTr("Not checked yet."); color: Theme.textDim }
                    RowLayout {
                        DarkButton { text: qsTr("Check again"); enabled: !plasma.busy; onClicked: plasma.refresh() }
                        DarkButton { text: qsTr("Copy report"); enabled: plasma.diagnostics.length > 0; onClicked: plasma.copyReport() }
                        DarkButton { text: qsTr("Restart service"); enabled: !center.chat.busy; onClicked: { center.desktop.restartDaemon(); refreshLater.restart() } }
                    }
                    Label { text: qsTr("The same check in a terminal: jarvis --diagnose · the service log: journalctl --user -u jarvis"); color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Label { text: qsTr("Restore the standard desktop"); color: Theme.text; font.pixelSize: 17; font.bold: true }
                    Label { text: qsTr("Puts Plasma's layout back exactly as it was before JARVIS DESKTOP (from the backup), or — without a backup — removes every Jarvis widget and panel. Your wallpaper, themes and KDE settings are not touched. If Jarvis itself does not start, run: jarvis --restore-desktop"); color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    DarkButton { objectName: "restoreDesktopButton"; text: qsTr("Restore the standard desktop…"); enabled: !plasma.busy && !shell.busy; onClicked: shellRestoreConsent.open() }
                }
            }
        }
        DarkButton { Layout.alignment: Qt.AlignRight; text: qsTr("Done"); onClicked: center.close() }
    }
    FileDialog { id: sampleDialog; title: qsTr("A recording of the voice"); nameFilters: [qsTr("Audio (*.wav *.flac *.mp3 *.ogg)")]; onAccepted: center.voice.setChatterboxSample(selectedFile) }
    FileDialog { id: skillFile; title: qsTr("Import skill"); nameFilters: [qsTr("Jarvis skills (*.json)")]; onAccepted: center.desktop.importSkill(selectedFile) }
    SkillPage {
        id: skillPage
        desktop: center.desktop
        chat: center.chat
        english: center.english
        onAskRequested: question => { skillPage.close(); center.close(); center.chat.send(question) }
        onAskWithFilesRequested: (question, files) => { skillPage.close(); center.close(); center.chat.sendWithFiles(question, files) }
    }
    Dialog {
        id: screenConsent
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Let Jarvis read your screen?")
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: 420
            wrapMode: Text.Wrap
            text: qsTr("Jarvis will take a screenshot of the window you work in when you ask (and, if you set an interval, now and then in the background) and recognise its text on this computer. \"◉ SCREEN\" is shown every time. Screenshots are not saved, password and private windows are skipped, and nothing goes to Claude unless you ask a question. You can turn this off or pause it at any moment.")
        }
        onAccepted: center.report(center.settings.setOption("awarenessScreen", true))
        onRejected: screenSwitch.checked = Qt.binding(() => center.settings.awarenessScreen)
    }
    PlasmaBridge {
        id: plasma
        onRestored: refreshLater.restart()
    }
    // The service and Plasma need a moment after a restart or a change.
    Timer { id: refreshLater; interval: 1500; onTriggered: plasma.refresh() }
    Dialog {
        id: addWidgetConsent
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Add the Jarvis widget to your desktop?")
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: 420
            wrapMode: Text.Wrap
            text: qsTr("Jarvis will ask Plasma to place one \"Jarvis HUD\" widget at the top right of the first screen's desktop. Nothing else in your layout changes. You can move it, resize it or remove it like any widget, or here.")
        }
        onAccepted: plasma.addWidget()
    }
    Dialog {
        id: removeWidgetConsent
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Remove the Jarvis widgets?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { width: 420; wrapMode: Text.Wrap; text: qsTr("Every \"Jarvis HUD\" widget leaves your desktops and panels. Other widgets are not touched.") }
        onAccepted: plasma.removeWidgets()
    }
    DesktopShell {
        id: shell
        onFinished: (ok, message) => { center.settings.reload(); plasma.refresh() }
    }
    ColorDialog {
        id: colourDialog
        property string key: "primary"
        title: qsTr("Desktop colour")
        onAccepted: center.report(center.settings.setDesktop(key, String(selectedColor).substring(0, 7)))
    }
    Dialog {
        id: applyConsent
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Turn on JARVIS DESKTOP?")
        standardButtons: Dialog.Yes | Dialog.No
        ColumnLayout {
            width: 480
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Jarvis will make these changes to your Plasma desktop:") }
            Repeater {
                model: shell.plan
                Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "• " + modelData; color: Theme.textDim; textFormat: Text.PlainText }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("If anything does not work, the changes are undone at once. You can always go back with \"Back to standard KDE\" or jarvis --restore-desktop.") }
        }
        onAccepted: shell.apply()
    }
    Dialog {
        id: shellRestoreConsent
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Back to the standard KDE desktop?")
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            width: 460; wrapMode: Text.Wrap
            text: shell.hasBackup ? qsTr("plasmashell restarts for a moment and Plasma's layout comes back from the backup made before JARVIS DESKTOP (%1). Changes you made to panels and widgets since then are saved aside, not kept. Your applications keep running.").arg(shell.backupPath)
                                  : qsTr("There is no backup: every Jarvis widget and panel is removed; nothing else changes.")
        }
        onAccepted: shell.restore()
    }
    Dialog {
        id: desktopsConsent
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Create virtual desktops for the workspaces?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { width: 440; wrapMode: Text.Wrap; text: qsTr("KWin gets as many virtual desktops as the workspaces need (named GENERAL, STUDY, WORK, DEV). Existing desktops are not renamed or removed.") }
        onAccepted: shell.createWorkspaceDesktops()
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
