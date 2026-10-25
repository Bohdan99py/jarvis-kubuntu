import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A list row: `values` and their `labels`, `value` bound to the saved value,
// `commit(value)` saves and returns "" or why not; the list then shows the
// saved value again.
SettingRow {
    id: row
    property var values: []
    property var labels: []
    property var value
    property int comboWidth: 260
    property var commit: function(v) { return "" }

    ComboBox {
        id: combo
        width: Math.min(row.comboWidth, row.width - 20)
        model: row.labels
        currentIndex: Math.max(0, row.values.indexOf(row.value))
        enabled: row.enabled
        Accessible.name: row.title
        contentItem: Text {
            // The style already keeps room for the arrow on the right.
            leftPadding: 12
            text: combo.displayText
            color: combo.enabled ? Theme.text : Theme.textDim
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 38
            radius: 10
            color: Theme.bg
            border.width: combo.visualFocus ? 2 : 1
            border.color: combo.visualFocus || combo.activeFocus ? Theme.accent : Theme.border
        }
        onActivated: index => {
            row.error = row.commit(row.values[index]) || ""
            currentIndex = Qt.binding(() => Math.max(0, row.values.indexOf(row.value)))
        }
    }
}
