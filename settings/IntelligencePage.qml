import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// Settings → Intelligence and connection: the Claude key and model, the
// connection check, web search, and what works without them.
ColumnLayout {
    id: page
    required property var win
    readonly property var settings: win.settings
    property string saveError
    property string saveDone

    spacing: 6

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        wrapMode: Text.Wrap
        color: Theme.textDim
        text: qsTr("Messages that Jarvis does not handle with local commands are sent to Anthropic (api.anthropic.com) together with the facts it remembers about you. Commands like \"memory\" or \"top processes\" stay on your computer.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        SettingGroup { title: qsTr("Claude") }
        SettingRow {
            entry: "ai.key"
            title: qsTr("Claude API key")
            hint: {
                switch (page.settings.keyStore) {
                case "keyring": return qsTr("Saved (%1) in the system password store (KWallet). Jarvis never writes it to a file, a log or a report.").arg(page.settings.keyHint)
                case "environment": return qsTr("Taken from the ANTHROPIC_API_KEY environment variable; that one wins over a saved key.")
                case "locked": return qsTr("The system password store is locked: unlock it to let Jarvis read the key.")
                case "file": return page.settings.keyringAvailable
                             ? qsTr("Saved (%1) in a file only you can read. Save it again to move it into the password store.").arg(page.settings.keyHint)
                             : qsTr("Saved (%1) in %2, readable by you alone: there is no system password store in this session.").arg(page.settings.keyHint).arg(page.settings.configPath)
                default: return qsTr("No key yet. Create one in the Claude Console (console.anthropic.com → API keys).")
                }
            }
            ColumnLayout {
                spacing: 6
                RowLayout {
                    TextField {
                        id: keyField
                        objectName: "apiKeyField"
                        Layout.preferredWidth: 250
                        // Hidden unless the user asks to see what they typed.
                        echoMode: showKey.checked ? TextInput.Normal : TextInput.Password
                        selectByMouse: true
                        placeholderText: page.settings.hasKey ? qsTr("New key (empty: keep the saved one)") : "sk-ant-…"
                        Accessible.name: qsTr("Claude API key")
                        onAccepted: saveButton.clicked()
                        onTextChanged: page.saveError = ""
                    }
                    CheckBox { id: showKey; text: qsTr("Show"); Accessible.name: qsTr("Show the key") }
                }
                Row {
                    spacing: 6
                    DarkButton { visible: page.settings.keyStore === "locked"; text: qsTr("Unlock…"); onClicked: page.saveError = page.settings.unlockKeyring() }
                    DarkButton {
                        text: qsTr("Remove key")
                        danger: true
                        visible: page.settings.hasKey && page.settings.keyStore !== "environment"
                        onClicked: removeConsent.open()
                    }
                }
            }
        }
        SettingRow {
            id: modelRow
            entry: "ai.model"
            title: qsTr("Model")
            hint: qsTr("Haiku is fast and inexpensive; Sonnet and Opus answer harder questions and cost more. You can type another model name.")
            ComboBox {
                id: modelField
                objectName: "modelField"
                width: Math.min(300, modelRow.width - 20)
                editable: true
                model: ["claude-haiku-4-5-20251001", "claude-sonnet-5-5", "claude-opus-5-5"]
                Component.onCompleted: editText = page.settings.model
                Connections { target: page.settings; function onSaved() { modelField.editText = page.settings.model } }
                Accessible.name: qsTr("Model")
                onEditTextChanged: page.saveError = ""
            }
        }
        SettingRow {
            entry: "ai.test"
            title: qsTr("Save and check")
            hint: qsTr("\"Save\" checks the format and stores the key and the model. \"Check the connection\" asks Anthropic whether the key works and the model exists (it does not use tokens); it runs only when you press it.")
            error: page.saveError
            ColumnLayout {
                spacing: 6
                Row {
                    spacing: 6
                    DarkButton {
                        id: saveButton
                        objectName: "apiSaveButton"
                        text: qsTr("Save")
                        primary: true
                        onClicked: {
                            page.saveDone = ""
                            const error = page.settings.save(keyField.text, modelField.editText)
                            page.saveError = error
                            // The typed key stays in the field when saving failed, so nothing has to be typed again.
                            if (!error) {
                                keyField.text = ""
                                page.saveDone = qsTr("Saved.")
                            }
                        }
                    }
                    DarkButton {
                        objectName: "apiTestButton"
                        text: page.settings.connectionState === "checking" ? qsTr("Checking…") : qsTr("Check the connection")
                        enabled: page.settings.connectionState !== "checking"
                        onClicked: page.settings.testConnection(keyField.text, modelField.editText)
                    }
                }
                Label {
                    objectName: "connectionResult"
                    Layout.preferredWidth: 300
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: page.settings.connectionState === "ok" ? Theme.accent : page.settings.connectionState === "checking" ? Theme.textDim : page.settings.connectionState === "warn" ? Theme.nodeFact : "#ff7b72"
                    text: page.saveDone.length > 0 && page.settings.connectionState === "idle" ? "✓ " + page.saveDone
                          : page.settings.connectionMessage.length === 0 ? ""
                          : (page.settings.connectionState === "ok" ? "✓ " : page.settings.connectionState === "checking" ? "… " : "⚠ ") + page.settings.connectionMessage
                }
            }
        }
        SettingSwitch {
            entry: "ai.websearch"
            enabled: page.settings.hasKey
            title: qsTr("Let Claude search the web for current facts")
            hint: qsTr("Traffic law that changed, racing results, calendars and regulations: Claude may run up to three web searches per answer (Anthropic's web search tool, billed per search; your organisation must allow it in the Claude Console). Off: answers come from Claude's knowledge and say when a value should be checked.")
            checked: page.settings.webSearch
            commit: on => page.settings.setOption("webSearch", on)
        }

        SettingGroup { title: qsTr("Without a key or internet") }
        SettingRow {
            entry: "ai.offline"
            title: qsTr("What works without a key or internet")
            hint: qsTr("Local commands (open applications, system state, memory), calculators and the skills' ready answers, reminders and tasks, dictation and the voices on this computer. Questions that need Claude say that a key or the internet is missing.")
        }
    }

    ConfirmDialog {
        id: removeConsent
        width: Math.min(460, page.win.width - 40)
        title: qsTr("Remove the Claude key?")
        acceptText: qsTr("Remove key")
        destructive: true
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("Jarvis forgets the key (from the password store and the settings file). Answers that need Claude stop until a key is saved again. Your memory and settings stay.") }
        onAccepted: page.saveError = page.settings.removeKey()
    }
}
