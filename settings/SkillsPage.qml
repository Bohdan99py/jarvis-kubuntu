import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Jarvis

// Settings → Skills: topics with calculators, formulas and ready answers.
ColumnLayout {
    id: page
    required property var win
    required property var skillPage
    readonly property var desktop: win.desktop

    spacing: 6

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        wrapMode: Text.Wrap
        color: Theme.textDim
        text: qsTr("Turn on the topics you need. Open a tile for its calculators, formulas and ready answers — they work without the internet.")
    }
    Flow {
        objectName: "entry:skills.list"
        Layout.fillWidth: true
        Layout.leftMargin: 18
        Layout.rightMargin: 18
        spacing: 12
        readonly property int columns: Math.max(1, Math.floor((width + spacing) / (220 + spacing)))
        readonly property int tileWidth: Math.floor((width - (columns - 1) * spacing) / columns)
        Repeater {
            model: page.desktop.skills
            Rectangle {
                id: tile
                required property var modelData
                readonly property color tint: modelData.color || Theme.accent
                width: parent.tileWidth
                height: 236
                radius: 16
                clip: true
                color: Theme.panel
                border.width: tileButton.visualFocus ? 2 : 1
                border.color: tileButton.visualFocus || tileButton.hovered ? tile.tint : Theme.border
                opacity: tile.modelData.enabled ? 1 : 0.7
                Image {
                    id: art
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 1
                    height: 100
                    source: "qrc:/icons/skills/" + (tile.modelData.image || "default") + ".svg"
                    sourceSize.width: 320
                    fillMode: Image.PreserveAspectCrop
                }
                // The whole tile opens the skill (also with the keyboard).
                Button {
                    id: tileButton
                    anchors.fill: parent
                    flat: true
                    background: null
                    Accessible.name: page.win.skillText(tile.modelData, "name")
                    onClicked: page.skillPage.show(tile.modelData)
                }
                ColumnLayout {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: art.bottom; anchors.bottom: parent.bottom
                    anchors.margins: 12; anchors.topMargin: 8
                    spacing: 3
                    Label { text: page.win.skillText(tile.modelData, "name"); color: Theme.text; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                    Label {
                        text: page.win.skillText(tile.modelData, "description")
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
                        Label { text: tile.modelData.enabled ? qsTr("On") : qsTr("Off"); color: tile.modelData.enabled ? Theme.accent : Theme.textDim; font.pixelSize: 12 }
                        Switch {
                            checked: tile.modelData.enabled
                            onClicked: page.desktop.setSkillEnabled(tile.modelData.id, checked)
                            Accessible.name: qsTr("Use %1").arg(page.win.skillText(tile.modelData, "name"))
                        }
                    }
                }
            }
        }
    }
    SettingRow {
        entry: "skills.import"
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        title: qsTr("Import a skill")
        hint: qsTr("A skill is a JSON file with a topic's guidance and ready answers. Imported skills start switched off.")
        ColumnLayout {
            spacing: 6
            DarkButton { text: qsTr("Import JSON…"); onClicked: skillFile.open() }
            RowLayout {
                TextField { id: skillUrl; Layout.preferredWidth: 230; placeholderText: "https://…/skill.json"; selectByMouse: true; Accessible.name: qsTr("Link to a skill") }
                DarkButton { text: qsTr("Fetch"); enabled: skillUrl.text.trim().length > 0; onClicked: page.desktop.fetchSkill(skillUrl.text.trim()) }
            }
        }
    }
    Label { text: page.desktop.skillStatus; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.bottomMargin: 24; textFormat: Text.PlainText; visible: text.length > 0 }
    FileDialog { id: skillFile; title: qsTr("Import skill"); nameFilters: [qsTr("Jarvis skills (*.json)")]; onAccepted: page.desktop.importSkill(selectedFile) }
}
