import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg

    // AppSettings instance
    required property var settings

    title: qsTr("Claude API settings")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 32 : 460, 460)
    padding: 20
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: 14
        color: Theme.panel
        border.width: 1
        border.color: Theme.border
    }

    header: Label {
        text: dlg.title
        color: Theme.text
        font.pixelSize: 17
        font.bold: true
        padding: 20
        bottomPadding: 0
    }

    onAboutToShow: {
        keyField.text = ""
        modelField.text = settings.model
        errorLabel.text = ""
    }

    function submit() {
        const err = settings.save(keyField.text, modelField.text)
        if (err.length > 0)
            errorLabel.text = err
        else
            dlg.close()
    }

    component FieldBackground: Rectangle {
        required property Item field
        radius: 10
        color: Theme.bg
        border.width: 1
        border.color: field.activeFocus ? Theme.accent : Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: 12
            text: qsTr("Messages that Jarvis does not handle with local commands are sent to Anthropic "
                       + "(api.anthropic.com) together with the facts it remembers about you. Commands like "
                       + "\"memory\" or \"top processes\" stay on your computer.")
        }

        Label {
            text: qsTr("API key")
            color: Theme.text
            font.pixelSize: 13
        }
        TextField {
            id: keyField
            Layout.fillWidth: true
            echoMode: TextInput.Password
            selectByMouse: true
            color: Theme.text
            placeholderTextColor: Theme.textDim
            placeholderText: dlg.settings.hasKey
                             ? qsTr("saved (%1) — leave empty to keep it").arg(dlg.settings.keyHint)
                             : "sk-ant-..."
            leftPadding: 12
            background: FieldBackground { field: keyField }
            onAccepted: dlg.submit()
        }

        Label {
            text: qsTr("Model")
            color: Theme.text
            font.pixelSize: 13
        }
        TextField {
            id: modelField
            Layout.fillWidth: true
            selectByMouse: true
            color: Theme.text
            leftPadding: 12
            background: FieldBackground { field: modelField }
            onAccepted: dlg.submit()
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: 11
            text: qsTr("For example: claude-haiku-4-5-20251001 (fast and inexpensive), "
                       + "claude-sonnet-5-5, claude-opus-5-5.")
        }

        Label {
            id: errorLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#ff7b72"
            font.pixelSize: 12
            visible: text.length > 0
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            color: Theme.textDim
            font.pixelSize: 11
            text: qsTr("File: %1 (owner-only access).").arg(dlg.settings.configPath)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 10

            DarkButton {
                text: qsTr("Remove key")
                danger: true
                visible: dlg.settings.hasKey
                onClicked: {
                    const err = dlg.settings.removeKey()
                    if (err.length > 0)
                        errorLabel.text = err
                    else
                        dlg.close()
                }
            }
            Item { Layout.fillWidth: true }
            DarkButton {
                text: qsTr("Cancel")
                onClicked: dlg.close()
            }
            DarkButton {
                text: qsTr("Save")
                primary: true
                onClicked: dlg.submit()
            }
        }
    }
}
