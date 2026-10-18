import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A code block from an answer: language or file name, Copy and Save, syntax colours.
Rectangle {
    id: block
    required property string code
    required property string lang
    required property string file
    required property var chat
    property string savedPath: ""

    implicitHeight: column.implicitHeight
    radius: 10
    color: "#0a1220"
    border.color: Theme.border

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 6
            Layout.preferredHeight: 30
            spacing: 4
            Label {
                text: block.file.length > 0 ? block.file : (block.lang.length > 0 ? block.lang : qsTr("code"))
                color: Theme.textDim; font.pixelSize: 11; font.family: "monospace"
                Layout.fillWidth: true; elide: Text.ElideMiddle
            }
            Repeater {
                model: [
                    { id: "copy", label: copied.running ? qsTr("Copied") : qsTr("Copy") },
                    { id: "save", label: block.savedPath.length > 0 ? qsTr("Saved — show") : qsTr("Save as file") }
                ]
                ToolButton {
                    required property var modelData
                    text: modelData.label
                    implicitHeight: 24
                    contentItem: Text { text: parent.text; color: parent.hovered ? Theme.accent : Theme.textDim; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 6; color: parent.hovered ? Theme.hover : "transparent" }
                    onClicked: {
                        if (modelData.id === "copy") {
                            block.chat.copyText(block.code)
                            copied.restart()
                        } else if (block.savedPath.length > 0) {
                            block.chat.showInFolder(block.savedPath)
                        } else {
                            block.savedPath = block.chat.saveFile(block.file, block.code, block.lang)
                        }
                    }
                    ToolTip.visible: hovered && modelData.id === "save" && block.savedPath.length > 0
                    ToolTip.text: block.savedPath
                }
            }
            Timer { id: copied; interval: 1500 }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        TextEdit {
            id: codeText
            Layout.fillWidth: true
            Layout.margins: 10
            text: block.code
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
            font.family: "monospace"
            font.pixelSize: 13
            color: "#d6e2ee"
            selectionColor: Theme.accent
            selectedTextColor: Theme.bg
        }
        CodeHighlighter { document: codeText.textDocument; language: block.lang }
    }
}
