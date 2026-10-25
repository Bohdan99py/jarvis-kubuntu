import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A switch row. `checked` is bound to the saved value; `commit(on)` saves and
// returns "" or why not. Afterwards the switch shows the saved value again —
// also when saving failed — and the words beside it say on or off, so the
// state is not shown by colour alone.
SettingRow {
    id: row
    property bool checked
    property var commit: function(on) { return "" }

    Row {
        spacing: 8
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: toggle.checked ? qsTr("On") : qsTr("Off")
            color: toggle.checked ? Theme.accent : Theme.textDim
            font.pixelSize: 12
        }
        Switch {
            id: toggle
            checked: row.checked
            enabled: row.enabled
            Accessible.name: row.title
            Accessible.description: row.hint
            onToggled: {
                row.error = row.commit(checked) || ""
                checked = Qt.binding(() => row.checked)
            }
        }
    }
}
