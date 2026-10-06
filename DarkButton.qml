import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool primary: false
    property bool danger: false

    implicitHeight: 38
    leftPadding: 18
    rightPadding: 18

    contentItem: Text {
        text: control.text
        color: control.enabled ? "white" : Theme.textDim
        font.pixelSize: 14
        font.bold: control.primary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 96
        radius: height / 2
        color: {
            if (!control.enabled)
                return Theme.border
            if (control.primary)
                return control.down ? Qt.darker(Theme.userBubble, 1.3)
                                    : control.hovered ? Qt.lighter(Theme.userBubble, 1.15)
                                                      : Theme.userBubble
            if (control.danger)
                return control.down ? Qt.darker(Theme.danger, 1.3)
                                    : control.hovered ? Theme.danger : Theme.botBubble
            return control.down ? Theme.border
                                : control.hovered ? Theme.hover : Theme.botBubble
        }
    }
}
