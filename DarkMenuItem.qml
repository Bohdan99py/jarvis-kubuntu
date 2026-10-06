import QtQuick
import QtQuick.Controls

MenuItem {
    id: control

    implicitHeight: 38

    contentItem: Text {
        leftPadding: 10
        text: control.text
        color: !control.enabled ? Theme.textDim : Theme.text
        font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 6
        color: control.highlighted && control.enabled ? Theme.hover : "transparent"
    }
}
