import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Jarvis

// Settings → Talking to Jarvis: languages, voice, microphone, keyboard
// shortcuts, chat and overlay, reminders.
ColumnLayout {
    id: page
    required property var win
    required property var kde
    readonly property var settings: win.settings
    readonly property var voice: win.voice
    // Read again after a key was changed.
    property int keysRead: 0
    readonly property var overlayKey: { keysRead; return kde.shortcut("org.jarvis.Jarvis", "overlay") }
    readonly property var quickKey: { keysRead; return kde.shortcut("org.jarvis.Jarvis.desktop", "Quick") }

    spacing: 6

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        // ---- Language ----
        SettingGroup { title: qsTr("Language") }
        SettingCombo {
            objectName: "languageCombo"
            entry: "language.ui"
            title: qsTr("Interface language")
            hint: qsTr("Applies at once to every Jarvis window.")
            values: ["auto", "ru", "en"]
            labels: [qsTr("System (%1)").arg(page.settings.uiLanguage === "ru" ? "Русский" : "English"), "Русский", "English"]
            value: page.settings.language
            commit: v => page.settings.setLanguage(v)
        }
        SettingCombo {
            entry: "language.reply"
            title: qsTr("Answer language")
            values: ["auto", "ru", "en"]
            labels: [qsTr("Same as my message"), "Русский", "English"]
            value: page.settings.replyLanguage
            commit: v => page.settings.setReplyLanguage(v)
        }

        // ---- Voice ----
        SettingGroup { title: qsTr("Voice") }
        SettingSwitch {
            entry: "voice.speak"
            title: qsTr("Speak answers aloud")
            hint: qsTr("Code, links and tables are not read out — only what a person would say. A new answer interrupts the one still being read.")
            checked: page.voice.enabled
            commit: on => { page.voice.enabled = on; return "" }
        }
        SettingCombo {
            entry: "voice.engine"
            title: qsTr("Voice engine")
            hint: {
                const names = { elevenlabs: "ElevenLabs", chatterbox: "Chatterbox", piper: "Piper", system: qsTr("the system voice") }
                return qsTr("Speaking now: %1. If it fails, the next ready voice takes over, so Jarvis is never silent.").arg(names[page.voice.activeEngine] || page.voice.activeEngine)
            }
            values: ["auto", "elevenlabs", "chatterbox", "piper", "system"]
            labels: [qsTr("Best available"), qsTr("ElevenLabs — studio voices (cloud)"), qsTr("Chatterbox — neural, on this computer"),
                     qsTr("Piper — light neural, on this computer"), qsTr("System voice (espeak-ng)")]
            value: page.voice.engine
            comboWidth: 330
            commit: v => { page.voice.engine = v; return "" }
        }
        SettingSlider {
            entry: "voice.pace"
            title: qsTr("Speaking pace")
            from: 0.7; to: 1.4
            value: page.voice.rate
            commit: v => { page.voice.rate = v; return "" }
        }
        SettingRow {
            title: qsTr("Listen")
            Flow {
                width: SettingsState.narrow ? page.width - 60 : 330
                spacing: 6
                DarkButton { text: qsTr("Russian"); onClicked: page.voice.preview("ru") }
                DarkButton { text: qsTr("English"); onClicked: page.voice.preview("en") }
                DarkButton { text: qsTr("French"); onClicked: page.voice.preview("fr") }
                DarkButton { text: qsTr("Stop"); enabled: page.voice.speaking || page.voice.recording; onClicked: page.voice.stop() }
            }
        }
        Label { text: page.voice.status; color: Theme.accent; Layout.fillWidth: true; Layout.leftMargin: 10; wrapMode: Text.Wrap; textFormat: Text.PlainText; visible: text.length > 0 }

        // ElevenLabs: cloud voices with the most natural intonation.
        SettingRow {
            id: elevenRow
            entry: "voice.elevenlabs"
            visible: page.voice.engine === "auto" || page.voice.engine === "elevenlabs"
            title: qsTr("ElevenLabs key")
            hint: (page.voice.elevenReady
                   ? (page.voice.elevenKeyStore === "keyring" ? qsTr("Saved (%1) in the system password store.").arg(page.voice.elevenKeyHint)
                      : page.voice.elevenKeyStore === "environment" ? qsTr("Taken from the ELEVENLABS_API_KEY environment variable.")
                      : qsTr("Saved (%1) in a file only you can read: there is no system password store here.").arg(page.voice.elevenKeyHint))
                   : qsTr("Studio voices in the cloud. The text of each answer is sent to ElevenLabs. Key: elevenlabs.io → Developers → API keys."))
            ColumnLayout {
                spacing: 6
                RowLayout {
                    TextField {
                        id: elevenKey
                        Layout.preferredWidth: 230
                        echoMode: showEleven.checked ? TextInput.Normal : TextInput.Password
                        selectByMouse: true
                        placeholderText: page.voice.elevenReady ? qsTr("New key") : qsTr("ElevenLabs API key")
                        Accessible.name: qsTr("ElevenLabs API key")
                        onAccepted: if (text.trim().length > 0) { page.voice.setElevenKey(text); text = "" }
                    }
                    CheckBox { id: showEleven; text: qsTr("Show"); Accessible.name: qsTr("Show the key") }
                }
                Row {
                    spacing: 6
                    DarkButton { text: qsTr("Save"); primary: true; enabled: elevenKey.text.trim().length > 0; onClicked: { page.voice.setElevenKey(elevenKey.text); elevenKey.text = "" } }
                    DarkButton { text: qsTr("Remove"); danger: true; visible: page.voice.elevenReady && page.voice.elevenKeyStore !== "environment"; onClicked: page.voice.setElevenKey("") }
                }
            }
        }
        SettingCombo {
            visible: page.voice.elevenReady && (page.voice.engine === "auto" || page.voice.engine === "elevenlabs")
            title: qsTr("ElevenLabs voice")
            readonly property var options: page.voice.elevenVoices.length > 0 ? page.voice.elevenVoices
                                           : [{ id: page.voice.elevenVoice, name: page.voice.elevenVoice === "JBFqnCBsd6RMkjVDRZzb" ? "George" : page.voice.elevenVoice, description: "" }]
            values: options.map(v => v.id)
            labels: options.map(v => v.description ? v.name + " — " + v.description : v.name)
            value: page.voice.elevenVoice
            comboWidth: 330
            commit: v => { const o = options.find(x => x.id === v); page.voice.chooseElevenVoice(v, o ? o.name : v); return "" }
        }
        SettingRow {
            visible: page.voice.elevenReady && (page.voice.engine === "auto" || page.voice.engine === "elevenlabs")
            title: qsTr("Your ElevenLabs voices")
            DarkButton { text: page.voice.elevenLoading ? qsTr("Loading…") : qsTr("Load my voices"); enabled: !page.voice.elevenLoading; onClicked: page.voice.loadElevenVoices() }
        }
        SettingCombo {
            visible: page.voice.elevenReady && (page.voice.engine === "auto" || page.voice.engine === "elevenlabs")
            title: qsTr("ElevenLabs model")
            values: ["eleven_multilingual_v2", "eleven_flash_v2_5", "eleven_turbo_v2_5", "eleven_v3"]
            labels: [qsTr("Multilingual v2 — the most natural reading"), qsTr("Flash v2.5 — starts fastest, half the price"),
                     qsTr("Turbo v2.5 — fast, good quality"), qsTr("v3 — the most expressive")]
            value: page.voice.elevenModel
            comboWidth: 330
            commit: v => { page.voice.elevenModel = v; return "" }
        }
        SettingSlider {
            visible: page.voice.elevenReady && (page.voice.engine === "auto" || page.voice.engine === "elevenlabs")
            title: qsTr("Stability")
            from: 0; to: 1
            value: page.voice.elevenStability
            valueText: v => v < 0.35 ? qsTr("lively") : v > 0.7 ? qsTr("even") : qsTr("balanced")
            commit: v => { page.voice.elevenStability = v; return "" }
        }

        // Chatterbox: open-source neural speech that can take on a voice from a recording.
        SettingRow {
            visible: page.voice.engine === "auto" || page.voice.engine === "chatterbox"
            title: qsTr("Chatterbox")
            hint: page.voice.chatterboxReady
                  ? qsTr("A neural voice on this computer; nothing is sent anywhere. Voice sample: %1.").arg(page.voice.chatterboxSample.length > 0 ? page.voice.chatterboxSample : qsTr("Chatterbox's own voice"))
                  : qsTr("A neural voice by Resemble AI that runs on this computer. Download: 1–3 GB for the program and about 3 GB for the model. %1")
                        .arg(page.voice.hasNvidia ? qsTr("An NVIDIA graphics card was found: the GPU build of PyTorch will be installed.")
                                                  : qsTr("No NVIDIA graphics card was found: the smaller processor build of PyTorch will be installed."))
            ColumnLayout {
                spacing: 6
                DarkButton { visible: !page.voice.chatterboxReady; text: qsTr("Install Chatterbox"); primary: true; enabled: !page.voice.busy; onClicked: page.voice.installChatterbox() }
                DarkButton { visible: page.voice.chatterboxReady; text: qsTr("Choose a recording…"); onClicked: sampleDialog.open() }
                DarkButton { visible: page.voice.chatterboxReady && page.voice.chatterboxSample.length > 0; text: qsTr("Remove the sample"); onClicked: page.voice.setChatterboxSample("") }
            }
        }
        SettingSlider {
            visible: page.voice.chatterboxReady && (page.voice.engine === "auto" || page.voice.engine === "chatterbox")
            title: qsTr("Expressiveness")
            from: 0.25; to: 1.5
            value: page.voice.chatterboxExaggeration
            valueText: v => v < 0.45 ? qsTr("calm") : v > 0.8 ? qsTr("dramatic") : qsTr("natural")
            commit: v => { page.voice.chatterboxExaggeration = v; return "" }
        }
        SettingCombo {
            visible: page.voice.chatterboxReady && (page.voice.engine === "auto" || page.voice.engine === "chatterbox")
            title: qsTr("Chatterbox runs on")
            values: ["auto", "cuda", "cpu"]
            labels: [qsTr("Graphics card if there is one"), qsTr("NVIDIA graphics card (CUDA)"), qsTr("Processor")]
            value: page.voice.chatterboxDevice
            commit: v => { page.voice.chatterboxDevice = v; return "" }
        }

        // Piper: light local neural voices, one per language.
        SettingRow {
            visible: page.voice.engine === "auto" || page.voice.engine === "piper"
            title: qsTr("Piper")
            hint: page.voice.naturalReady
                  ? qsTr("A neural voice (Piper) speaks on this computer: no internet, nothing is sent anywhere. Choose a voice for each language and listen to it.")
                  : qsTr("Instead of the robotic system voice, a neural voice (Piper) that sounds like a person. It runs on this computer without the internet. The download is about 200 MB for the program and ~63 MB per voice.")
            DarkButton {
                readonly property bool missing: page.voice.voices.some(v => !v.installed && Object.values(page.voice.chosenVoices).indexOf(v.id) >= 0)
                visible: !page.voice.naturalReady || missing
                text: page.voice.naturalReady ? qsTr("Download the chosen voices") : qsTr("Install the natural voice")
                primary: true
                enabled: !page.voice.busy
                onClicked: page.voice.installNatural()
            }
        }
        Repeater {
            model: (page.voice.engine === "auto" || page.voice.engine === "piper")
                   ? [{ lang: "ru", label: qsTr("Russian voice") }, { lang: "en", label: qsTr("English voice") }, { lang: "fr", label: qsTr("French voice (studies)") }] : []
            SettingCombo {
                required property var modelData
                readonly property var options: page.voice.voices.filter(v => v.lang === modelData.lang)
                title: modelData.label
                values: options.map(v => v.id)
                labels: options.map(v => v.name + (v.installed ? "" : "  ⤓"))
                value: page.voice.chosenVoices[modelData.lang]
                commit: v => { page.voice.chooseVoice(modelData.lang, v); return "" }
            }
        }

        // ---- Microphone ----
        SettingGroup { title: qsTr("Microphone") }
        SettingRow {
            entry: "voice.dictation"
            title: qsTr("Dictation")
            hint: (page.voice.ready ? qsTr("Local recognition is ready. Press the microphone in the chat (or Ctrl+Shift+M), speak and press stop.")
                                    : qsTr("Setup downloads Vosk from PyPI and a model (~45 MB) from alphacephei.com for the interface language. After that, speech is recognised on this computer."))
                  + " " + qsTr("The microphone is the one chosen in KDE's sound settings.")
            DarkButton { text: page.voice.ready ? qsTr("Check the installation") : qsTr("Install dictation"); enabled: !page.voice.busy; onClicked: page.voice.setup() }
        }
        KdeLink {
            entry: "kde.sound"
            kcm: "kcm_pulseaudio"
            title: qsTr("Microphone, speakers and headphones")
            description: qsTr("Opens System Settings → Sound.")
        }

        // ---- Keyboard shortcuts ----
        SettingGroup {
            title: qsTr("Keyboard shortcuts")
            text: qsTr("Global keys work in any application. Jarvis checks whether a key is already taken and never takes it from another action.")
        }
        SettingRow {
            entry: "shortcuts.overlay"
            title: qsTr("Key for the overlay")
            hint: page.overlayKey.keys ? qsTr("Now: %1").arg(page.overlayKey.keys)
                  : (page.win.shortcut && page.win.shortcut.status) ? page.win.shortcut.status
                  : qsTr("Not set. Without a key the overlay opens from the tray or with jarvis --overlay.")
            Row {
                spacing: 6
                DarkButton {
                    visible: !page.overlayKey.keys && !!page.win.shortcut && !page.win.shortcut.bound
                    text: qsTr("Bind Ctrl+J")
                    onClicked: { page.win.shortcut.bind(); page.keysRead++ }
                }
                DarkButton {
                    text: qsTr("Change…")
                    enabled: page.overlayKey.available
                    onClicked: keyDialog.ask("org.jarvis.Jarvis", "overlay", qsTr("Key for the overlay"))
                }
            }
        }
        SettingRow {
            entry: "shortcuts.quick"
            title: qsTr("Key for quick commands")
            hint: page.quickKey.available ? (page.quickKey.keys ? qsTr("Now: %1").arg(page.quickKey.keys) : qsTr("Not set."))
                                          : qsTr("KDE's shortcut service is not available.")
            DarkButton {
                text: qsTr("Change…")
                enabled: page.quickKey.available
                onClicked: keyDialog.ask("org.jarvis.Jarvis.desktop", "Quick", qsTr("Key for quick commands"))
            }
        }
        SettingRow {
            entry: "shortcuts.window"
            title: qsTr("Keys inside the Jarvis window")
            hint: qsTr("Ctrl+J — overlay · Ctrl+M — memory panel · Ctrl+Shift+O — compact core · Ctrl+Shift+M — dictation · Ctrl+O — attach files · Ctrl+F — search in settings. These keys are fixed.")
        }
        KdeLink {
            entry: "kde.shortcuts"
            kcm: "kcm_keys"
            title: qsTr("All keyboard shortcuts")
            description: qsTr("Opens System Settings → Shortcuts, where every key of the system can be changed.")
        }

        // ---- Chat and overlay ----
        SettingGroup { title: qsTr("Chat and overlay") }
        SettingSwitch {
            entry: "chat.notify"
            title: qsTr("Tell me when an answer is ready while Jarvis is hidden")
            hint: qsTr("An answer you were waiting for, a finished transcription or PDF translation, the voice setup.")
            checked: page.settings.notifyReplies
            commit: on => page.settings.setOption("notifyReplies", on)
        }
        SettingSwitch {
            objectName: "overlaySwitch"
            entry: "chat.overlay"
            title: qsTr("Compact core above other windows")
            hint: qsTr("A small always-on-top core that shows what Jarvis is doing. Drag it anywhere; a click opens Jarvis. Ctrl+Shift+O switches it.")
            checked: page.settings.overlay
            commit: on => { page.settings.overlay = on; return "" }
        }

        // ---- Reminders ----
        SettingGroup {
            title: qsTr("Reminders")
            text: qsTr("Reminders always come, even with the window closed. Try in chat: \"remind me tomorrow at 9 to call mum\", \"every weekday at 8:30 exercise\".")
        }
        SettingCombo {
            entry: "reminders.snooze"
            title: qsTr("\"Snooze\" button")
            values: [5, 10, 15, 30, 60]
            labels: values.map(v => v < 60 ? qsTr("%1 minutes").arg(v) : qsTr("1 hour"))
            value: page.settings.snoozeMinutes
            commit: v => page.settings.setNumber("snoozeMinutes", v)
        }
        SettingCombo {
            entry: "reminders.repeat"
            title: qsTr("Remind again until I press \"Done\"")
            values: [0, 5, 10, 15, 30]
            labels: values.map(v => v === 0 ? qsTr("remind once") : qsTr("again every %1 min (3 times)").arg(v))
            value: page.settings.nagMinutes
            commit: v => page.settings.setNumber("nagMinutes", v)
        }
        SettingCombo {
            entry: "reminders.times"
            title: qsTr("\"In the morning\" means")
            values: [5, 6, 7, 8, 9, 10, 11]
            labels: values.map(v => v + ":00")
            value: page.settings.morningHour
            comboWidth: 140
            commit: v => page.settings.setNumber("morningHour", v)
        }
        SettingCombo {
            title: qsTr("\"In the afternoon\" means")
            values: [12, 13, 14, 15, 16, 17]
            labels: values.map(v => v + ":00")
            value: page.settings.afternoonHour
            comboWidth: 140
            commit: v => page.settings.setNumber("afternoonHour", v)
        }
        SettingCombo {
            title: qsTr("\"In the evening\" means")
            values: [17, 18, 19, 20, 21, 22]
            labels: values.map(v => v + ":00")
            value: page.settings.eveningHour
            comboWidth: 140
            commit: v => page.settings.setNumber("eveningHour", v)
        }
        SettingSwitch {
            entry: "reminders.sound"
            title: qsTr("Sound with reminders")
            hint: qsTr("The alarm sound of your Plasma sound theme.")
            checked: page.settings.reminderSound
            commit: on => page.settings.setOption("reminderSound", on)
        }
        SettingRow {
            title: qsTr("Test")
            hint: qsTr("Shows how reminders will look.")
            DarkButton { text: qsTr("Send a test notification"); onClicked: notifier.show("test", "Jarvis", qsTr("Notifications work. This is how reminders will look."), [["default", ""]]) }
        }
        KdeLink {
            entry: "kde.notifications"
            kcm: "kcm_notifications"
            title: qsTr("Notifications and Do Not Disturb")
            description: qsTr("Opens System Settings → Notifications. Jarvis follows Plasma's Do Not Disturb.")
        }
    }

    FileDialog { id: sampleDialog; title: qsTr("A recording of the voice"); nameFilters: [qsTr("Audio (*.wav *.flac *.mp3 *.ogg)")]; onAccepted: page.voice.setChatterboxSample(selectedFile) }

    // A new key: pressed, checked against every other global shortcut, then set.
    Dialog {
        id: keyDialog
        property string component
        property string action
        property int key: 0
        property int modifiers: 0
        readonly property string text: key ? page.kde.keyText(key, modifiers) : ""
        readonly property var conflict: key ? page.kde.conflict(key, modifiers, component, action) : ({})
        readonly property bool usable: key !== 0 && !conflict.invalid && !conflict.taken
        property string error
        function ask(c, a, title) {
            component = c; action = a; key = 0; modifiers = 0; error = ""
            keyDialog.title = title
            open()
            catcher.forceActiveFocus()
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, page.win.width - 40)
        modal: true
        closePolicy: Popup.NoAutoClose
        // Recording: KDE's global keys stay quiet so they can be pressed and checked.
        onOpened: page.kde.blockGlobalShortcuts(true)
        onClosed: page.kde.blockGlobalShortcuts(false)
        Component.onDestruction: page.kde.blockGlobalShortcuts(false)
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Press the new key combination, for example Ctrl+Alt+J.") }
            Rectangle {
                id: catcher
                objectName: "keyCatcher"
                Layout.fillWidth: true
                implicitHeight: 54
                radius: 10
                color: Theme.bg
                border.width: 2
                border.color: activeFocus ? Theme.accent : Theme.border
                focus: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Press the new key combination")
                Label {
                    anchors.centerIn: parent
                    text: keyDialog.text || qsTr("waiting for a key…")
                    color: keyDialog.text ? Theme.text : Theme.textDim
                    font.pixelSize: 18
                }
                Keys.onPressed: event => {
                    // Tab moves on to the buttons; a key alone (without Ctrl/Alt/Meta) is refused below.
                    if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                        return
                    event.accepted = true
                    if (page.kde.keyText(event.key, event.modifiers).length === 0)
                        return
                    keyDialog.key = event.key
                    keyDialog.modifiers = event.modifiers
                    keyDialog.error = ""
                }
            }
            Label {
                objectName: "keyConflict"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: keyDialog.key !== 0
                color: keyDialog.usable ? Theme.accent : "#ff7b72"
                text: keyDialog.conflict.invalid ? "⚠ " + qsTr("Use Ctrl, Alt or Meta together with a key (or an F-key alone).")
                      : keyDialog.conflict.taken ? "⚠ " + qsTr("%1 is already used by %2. Choose another key, or free it in System Settings → Shortcuts.").arg(keyDialog.text).arg(keyDialog.conflict.owner)
                      : keyDialog.conflict.self ? "✓ " + qsTr("This is already the key of this action.")
                      : keyDialog.conflict.unknown ? "⚠ " + qsTr("KDE's shortcut service did not answer; the key cannot be checked.")
                      : "✓ " + qsTr("Free: no other action uses %1.").arg(keyDialog.text)
                textFormat: Text.PlainText
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; visible: keyDialog.error.length > 0; color: "#ff7b72"; text: "⚠ " + keyDialog.error; textFormat: Text.PlainText }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                DarkButton {
                    text: qsTr("Default")
                    onClicked: {
                        const d = page.kde.defaultKey(keyDialog.action)
                        keyDialog.key = d.key; keyDialog.modifiers = d.modifiers; keyDialog.error = ""
                    }
                }
                DarkButton { text: qsTr("Cancel"); onClicked: keyDialog.close() }
                DarkButton {
                    objectName: "keyApply"
                    text: qsTr("Apply")
                    primary: true
                    enabled: keyDialog.usable
                    onClicked: {
                        const error = page.kde.setShortcut(keyDialog.component, keyDialog.action, "", "", keyDialog.key, keyDialog.modifiers)
                        page.keysRead++
                        if (error) keyDialog.error = error
                        else keyDialog.close()
                    }
                }
            }
        }
    }
}
