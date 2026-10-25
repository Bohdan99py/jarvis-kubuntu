import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A slider row; `commit` runs when the handle is released (or moved with the
// keyboard), `valueText(v)` describes the value beside it.
SettingRow {
    id: row
    property real from: 0
    property real to: 1
    property real stepSize: 0.05
    property real value
    property var valueText: function(v) { return Math.round(v * 100) + " %" }
    property var commit: function(v) { return "" }

    Row {
        spacing: 10
        Slider {
            id: slider
            width: Math.min(220, row.width - 90)
            from: row.from
            to: row.to
            stepSize: row.stepSize
            value: row.value
            enabled: row.enabled
            Accessible.name: row.title
            onMoved: if (!pressed) save()
            onPressedChanged: if (!pressed) save()
            function save() {
                row.error = row.commit(value) || ""
                value = Qt.binding(() => row.value)
            }
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            width: 64
            text: row.valueText(slider.value)
            color: Theme.textDim
        }
    }
}
