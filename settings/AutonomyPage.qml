import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// Settings → Autonomy (0.22): how Jarvis carries out tasks on the laptop — the
// mode (only local or automatic), the local model and its download, the voice
// on this computer, what each kind of action may do without asking, limits,
// and the diagnostics of what works here.
ColumnLayout {
    id: page
    required property var win
    readonly property var settings: win.settings
    readonly property bool english: win.english
    property string removeError

    spacing: 6

    AgentPanel { id: panel }

    function gb(v) { return (Math.round(v * 100) / 100).toLocaleString(Qt.locale(english ? "en" : "ru"), "f", 2) }
    function stateText(s) {
        switch (s) {
        case "ready": return qsTr("installed and verified")
        case "unverified": return qsTr("downloaded, not verified yet")
        case "partial": return qsTr("download interrupted — it resumes")
        case "corrupt": return qsTr("damaged (wrong size) — download it again")
        default: return qsTr("not downloaded")
        }
    }
    readonly property var chosen: {
        for (let i = 0; i < panel.models.length; ++i)
            if (panel.models[i].id === settings.agentModel)
                return panel.models[i]
        return null
    }
    readonly property var server: panel.daemon.model || ({})

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        wrapMode: Text.Wrap
        color: Theme.textDim
        text: qsTr("Say what you want done — \"play the sound through my headphones\", \"volume 30 and open the player\" — and Jarvis works out the steps, checks each is allowed, carries them out on this laptop and reads back what really changed. Everyday tasks work without the internet.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        // ---- Mode ----
        SettingGroup { title: qsTr("Mode") }
        SettingCombo {
            objectName: "agentModeCombo"
            entry: "autonomy.mode"
            title: qsTr("Where requests are understood")
            hint: page.settings.agentMode === "local"
                  ? qsTr("Only local: nothing goes to any cloud model — not Claude, not the ElevenLabs voice, not even a key check. Laptop tasks, the local model and the voice on this computer keep working.")
                  : qsTr("Automatic: laptop tasks are always understood and done on this computer. Free questions go to Claude when a key is saved; without one, to the local model. Having a network never by itself means data may go to the cloud.")
            values: ["auto", "local"]
            labels: [qsTr("Automatic"), qsTr("Only local")]
            value: page.settings.agentMode
            commit: v => page.settings.setAgent("mode", v)
        }
        SettingSwitch {
            entry: "autonomy.cloud"
            enabled: page.settings.agentMode === "auto" && page.settings.hasKey
            title: qsTr("Let Claude plan tasks the local model cannot")
            hint: page.settings.hasKey
                  ? qsTr("Only in automatic mode and only for laptop tasks the rules and the local model do not understand: the request and the names of your sound and Bluetooth devices go to Anthropic. Claude only proposes the steps; Jarvis checks and carries them out exactly as for the local model. No answer in 20 seconds: nothing is done and nothing is sent again.")
                  : qsTr("Needs a Claude key (Settings → Intelligence).")
            checked: page.settings.agentCloud
            commit: on => page.settings.setAgent("cloud", on)
        }

        // ---- The local model ----
        SettingGroup { title: qsTr("Local model") }
        SettingRow {
            objectName: "agentModelRow"
            entry: "autonomy.model"
            title: qsTr("Model for free wording")
            hint: qsTr("Simple commands work without any model. The model lets Jarvis understand any wording, several actions at once and plain conversation offline. It is downloaded once (only when you press the button) and checked against its published checksum; it is never part of the Jarvis package.")
            error: panel.jobError || page.removeError
            ColumnLayout {
                spacing: 8
                Repeater {
                    model: panel.models
                    delegate: ColumnLayout {
                        required property var modelData
                        spacing: 4
                        Layout.preferredWidth: 330
                        RowLayout {
                            spacing: 8
                            RadioButton {
                                checked: page.settings.agentModel === modelData.id
                                text: modelData.name + (modelData.recommended ? " · " + qsTr("recommended") : "")
                                onToggled: if (checked) page.settings.setAgent("model", modelData.id)
                                Accessible.name: modelData.name
                            }
                        }
                        Label {
                            Layout.preferredWidth: 320
                            leftPadding: 30
                            wrapMode: Text.Wrap
                            color: Theme.textDim
                            font.pixelSize: 12
                            text: qsTr("%1 GB · %2 · %3").arg(page.gb(modelData.sizeGb)).arg(modelData.license).arg(page.stateText(modelData.state))
                                  + "\n" + (page.english ? modelData.noteEn : modelData.noteRu)
                        }
                        Row {
                            leftPadding: 30
                            spacing: 6
                            visible: !(panel.job.id === modelData.id)
                            DarkButton {
                                visible: modelData.state !== "ready" && modelData.state !== "unverified"
                                text: modelData.state === "partial" ? qsTr("Resume download") : qsTr("Download…")
                                enabled: !panel.job.id
                                onClicked: { downloadConsent.target = modelData; downloadConsent.open() }
                            }
                            DarkButton {
                                visible: modelData.state === "unverified" || modelData.state === "ready"
                                text: qsTr("Verify")
                                enabled: !panel.job.id
                                onClicked: panel.verify(modelData.id)
                            }
                            DarkButton {
                                visible: modelData.state !== "missing"
                                text: qsTr("Remove")
                                danger: true
                                enabled: !panel.job.id
                                onClicked: page.removeError = panel.remove(modelData.id)
                            }
                        }
                        ColumnLayout {
                            visible: panel.job.id === modelData.id
                            spacing: 4
                            Layout.leftMargin: 30
                            ProgressBar {
                                Layout.preferredWidth: 260
                                from: 0
                                to: Math.max(1, panel.job.total || 1)
                                value: panel.job.done || 0
                                Accessible.name: qsTr("Download progress")
                            }
                            RowLayout {
                                Label {
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                    text: (panel.job.phase === "verify" ? qsTr("Checking the checksum… %1 of %2 GB") : qsTr("Downloading… %1 of %2 GB"))
                                          .arg(page.gb((panel.job.done || 0) / 1e9)).arg(page.gb((panel.job.total || 0) / 1e9))
                                }
                                DarkButton { text: qsTr("Stop"); onClicked: panel.cancel() }
                            }
                        }
                    }
                }
            }
        }
        SettingRow {
            entry: "autonomy.engine"
            title: qsTr("llama.cpp (runs the model)")
            hint: panel.llamaServer.length > 0
                  ? qsTr("Installed: %1. It starts only when a request needs the model, listens on this computer only (127.0.0.1) and stops after the idle time below.").arg(panel.llamaServer)
                  : qsTr("Not installed. In a terminal: sudo apt install llama.cpp-tools libggml0-backend-vulkan (Ubuntu 26.04). Until then simple commands work, free wording does not.")
        }
        SettingRow {
            objectName: "agentModelState"
            entry: "autonomy.state"
            title: qsTr("State")
            hint: {
                if (!page.chosen)
                    return qsTr("No model chosen.")
                const lines = []
                lines.push(page.chosen.state === "ready" && panel.llamaServer.length > 0
                           ? "✓ " + qsTr("Ready to work offline.")
                           : "✗ " + qsTr("Not ready offline yet: %1.").arg(page.chosen.state !== "ready" ? page.stateText(page.chosen.state) : qsTr("llama.cpp is missing")))
                if (!panel.daemon.mode)
                    lines.push("– " + qsTr("The Jarvis service is not running: the model is loaded by it."))
                else if (page.server.server === "ready")
                    lines.push("● " + qsTr("Loaded on %1; last start %2 s, last answer %3 s.").arg(page.server.device === "cpu" ? qsTr("the processor") : qsTr("the graphics card (%1)").arg(page.server.device))
                               .arg((page.server.startMs / 1000).toFixed(1)).arg(page.server.requestMs >= 0 ? (page.server.requestMs / 1000).toFixed(1) : "—"))
                else if (page.server.server === "starting")
                    lines.push("… " + qsTr("Loading…"))
                else if (page.server.server === "failed")
                    lines.push("✗ " + qsTr("Did not start: %1").arg(page.server.error))
                else
                    lines.push("○ " + qsTr("Not loaded (it loads with the first request that needs it)."))
                return lines.join("\n")
            }
        }
        SettingCombo {
            entry: "autonomy.device"
            title: qsTr("Run the model on")
            hint: qsTr("Automatic: the discrete graphics card when it has room, otherwise the processor (slower, still offline). The processor only: the graphics card stays asleep, which saves battery.")
            values: ["auto", "cpu"]
            labels: [qsTr("Automatic"), qsTr("The processor only")]
            value: page.settings.agentDevice
            commit: v => page.settings.setAgent("device", v)
        }
        SettingSlider {
            entry: "autonomy.keep"
            title: qsTr("Keep the model loaded after a request")
            hint: qsTr("Then it is unloaded and frees the memory; the next request loads it again (a few seconds).")
            from: 1; to: 60; stepSize: 1
            value: page.settings.agentKeepMinutes
            valueText: v => qsTr("%1 min").arg(Math.round(v))
            commit: v => page.settings.setAgent("keepMinutes", Math.round(v))
        }

        // ---- Voice ----
        SettingGroup { title: qsTr("Voice on this computer") }
        SettingRow {
            entry: "autonomy.voice"
            title: qsTr("Dictation and speech without the internet")
            hint: {
                const v = panel.voice
                const lines = []
                lines.push(v.dictationEngine && v.dictation.length > 0
                           ? "✓ " + qsTr("Dictation (Vosk): %1.").arg(v.dictation.join(", "))
                           : "✗ " + qsTr("Dictation is not installed: Settings → Talking to Jarvis → Install voice (about 45 MB, once)."))
                lines.push(v.natural ? "✓ " + qsTr("Natural voice (Piper): %1 voices.").arg(v.voices)
                                     : "– " + qsTr("Natural voice (Piper) is not installed (about 260 MB, once); the system voice speaks meanwhile."))
                lines.push(v.system ? "✓ " + qsTr("System voice (Speech Dispatcher): works offline.") : "✗ " + qsTr("No system voice (speech-dispatcher)."))
                lines.push(qsTr("The microphone listens only while you hold dictation (button or Ctrl+Shift+M); while Jarvis speaks it does not listen, so it never answers itself. The recognised text can be edited before sending; Esc stops speech."))
                return lines.join("\n")
            }
        }

        // ---- Permissions ----
        SettingGroup { title: qsTr("What Jarvis may do") }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: 12
            text: qsTr("\"Allow\": done at once, then checked. \"Ask\": Jarvis asks \"yes or no\" first. \"Off\": never. These are Jarvis's own permissions; the system's (polkit) still apply, and Jarvis never runs as administrator, never asks for or keeps your password and never runs commands a model wrote. Text from devices, windows and tools never changes these settings.")
        }
        Repeater {
            model: [
                { key: "audio", title: qsTr("Sound"), hint: qsTr("Output device, volume, mute, microphone.") },
                { key: "bluetooth", title: qsTr("Bluetooth"), hint: qsTr("Switch the adapter on, connect and disconnect known devices. Pairing new ones always needs you.") },
                { key: "apps", title: qsTr("Starting applications"), hint: qsTr("Start an installed application or switch to its window.") },
                { key: "apps.close", title: qsTr("Closing applications"), hint: qsTr("Asks the window to close like its close button (the application may ask to save). Never forced. A model's plan always asks first.") },
                { key: "screen", title: qsTr("Screen and notifications"), hint: qsTr("Brightness, do not disturb.") },
                { key: "desktop", title: qsTr("Desktop"), hint: qsTr("Settings pages, Jarvis profiles, the media player.") },
                { key: "power", title: qsTr("Power"), hint: qsTr("Power profile (the system allows it in your session).") },
                { key: "info", title: qsTr("System state"), hint: qsTr("Battery, network, devices, diagnostics — reading only.") }
            ]
            delegate: SettingCombo {
                required property var modelData
                entry: "autonomy.permission." + modelData.key
                title: modelData.title
                hint: modelData.hint
                comboWidth: 160
                values: ["allow", "ask", "off"]
                labels: [qsTr("Allow"), qsTr("Ask"), qsTr("Off")]
                value: page.settings.agentPermissions[modelData.key]
                commit: v => page.settings.setAgent("permission:" + modelData.key, v)
            }
        }

        // ---- Limits ----
        SettingGroup { title: qsTr("Limits") }
        SettingSlider {
            entry: "autonomy.steps"
            title: qsTr("Steps per task at most")
            hint: qsTr("A longer plan is cut here and Jarvis says so.")
            from: 1; to: 12; stepSize: 1
            value: page.settings.agentMaxSteps
            valueText: v => Math.round(v)
            commit: v => page.settings.setAgent("maxSteps", Math.round(v))
        }
        SettingSlider {
            entry: "autonomy.time"
            title: qsTr("Time per task at most")
            hint: qsTr("When it runs out, the current step is stopped where it can be and no other step starts. Each step has its own shorter limit too.")
            from: 10; to: 300; stepSize: 10
            value: page.settings.agentMaxSeconds
            valueText: v => qsTr("%1 s").arg(Math.round(v))
            commit: v => page.settings.setAgent("maxSeconds", Math.round(v))
        }

        // ---- Diagnostics ----
        SettingGroup { title: qsTr("Diagnostics") }
        SettingRow {
            objectName: "agentDiagnostics"
            entry: "autonomy.diagnostics"
            title: qsTr("What works on this computer")
            hint: {
                if (panel.diagnostics.length === 0)
                    return qsTr("Checks sound, Bluetooth, brightness, do not disturb, windows (KWin), power, the network and the local model — on this computer, nothing is sent.")
                const names = { audio: qsTr("Sound"), bluetooth: qsTr("Bluetooth"), brightness: qsTr("Brightness"), dnd: qsTr("Do not disturb"),
                                windows: qsTr("Windows"), power: qsTr("Power profiles"), network: qsTr("Network"), model: qsTr("Local model"),
                                voice: qsTr("Voice"), service: qsTr("Jarvis service") }
                return panel.diagnostics.map(d => (d.ok ? "✓ " : "✗ ") + (names[d.name] || d.name) + (d.detail ? " — " + d.detail : "")).join("\n")
            }
            DarkButton { objectName: "agentDiagnoseButton"; text: qsTr("Check"); onClicked: panel.runDiagnostics() }
        }
        SettingRow {
            entry: "autonomy.task"
            title: qsTr("Last task")
            visible: !!panel.daemon.task && !!panel.daemon.task.text
            hint: panel.daemon.task ? qsTr("«%1» — %2").arg(panel.daemon.task.text || "").arg(panel.daemon.task.status || "") : ""
        }
    }

    ConfirmDialog {
        id: downloadConsent
        property var target: null
        width: Math.min(480, page.win.width - 40)
        title: qsTr("Download %1?").arg(target ? target.name : "")
        acceptText: qsTr("Download")
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: downloadConsent.target
                  ? qsTr("Size: %1 GB, downloaded once from Hugging Face (%2), license %3. It lets Jarvis understand free wording and plain conversation offline. Needs about %4 GB of graphics memory or %5 GB of RAM while loaded. Saved in ~/.local/share/jarvis/models and checked against its sha256; an interrupted download resumes.")
                    .arg(page.gb(downloadConsent.target.sizeGb)).arg(downloadConsent.target.source).arg(downloadConsent.target.license)
                    .arg((downloadConsent.target.vramMb / 1000).toFixed(1)).arg((downloadConsent.target.ramMb / 1000).toFixed(1))
                  : ""
        }
        onAccepted: if (target) panel.download(target.id)
    }
}
