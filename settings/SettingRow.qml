import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// One setting: a title, a short explanation, and its control on the right (or
// under the text when the window is narrow, so a long title never hides it).
// `entry` is its id in the settings catalog: the search finds the row by
// objectName "entry:<id>" and outlines it. `note` says when a change applies,
// if not at once; `error` shows why the last change was not saved.
Item {
    id: row
    property string entry
    property string title
    property string hint
    property string note
    property string error
    default property alias control: slot.data
    readonly property bool highlighted: entry.length > 0 && SettingsState.highlight === entry
    // Any element of the row has the keyboard focus.
    readonly property bool focused: {
        let item = Window.activeFocusItem
        while (item) {
            if (item === row)
                return true
            item = item.parent
        }
        return false
    }

    objectName: entry.length > 0 ? "entry:" + entry : ""
    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight + 16
    implicitWidth: 300

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: row.highlighted ? Theme.selected : "transparent"
        border.width: row.highlighted ? 2 : (row.focused ? 1 : 0)
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: 200 } }
    }

    GridLayout {
        id: grid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        columns: SettingsState.narrow ? 1 : 2
        columnSpacing: 16
        rowSpacing: 6

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 120
            spacing: 2
            Label {
                text: row.title
                visible: text.length > 0
                color: row.enabled ? Theme.text : Theme.textDim
                font.pixelSize: 15
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: row.hint
                visible: text.length > 0
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: "↻ " + row.note
                visible: row.note.length > 0
                color: Theme.nodeFact
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: "⚠ " + row.error
                visible: row.error.length > 0
                color: "#ff7b72"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                textFormat: Text.PlainText
            }
        }
        Item {
            id: slot
            Layout.alignment: SettingsState.narrow ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }
}
