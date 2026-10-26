import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A group heading inside a page, with an optional line of explanation.
ColumnLayout {
    property alias title: heading.text
    property string text
    Layout.fillWidth: true
    Layout.topMargin: 18
    spacing: 4
    Label { id: heading; color: Theme.text; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap; Accessible.role: Accessible.Heading }
    Label { text: parent.text; visible: text.length > 0; color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
}
