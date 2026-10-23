import QtQuick
import Jarvis
import QtQuick.Controls

// A button in the HUD's style: a thin frame, mono label, cyan when on,
// amber when primary, red when it stops something. Reachable with Tab and
// pressed with Space or Enter.
AbstractButton {
    id: button
    property string tip
    property bool on: false
    property bool primary: false
    property bool danger: false
    readonly property color tint: danger ? JarvisTheme.red : primary ? JarvisTheme.amber : JarvisTheme.cyan

    implicitWidth: Math.max(38, label.implicitWidth + 22)
    implicitHeight: 38
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Text {
        id: label
        text: button.text
        color: !button.enabled ? Qt.rgba(1, 1, 1, 0.3) : button.on || button.primary || button.danger ? JarvisTheme.ice : JarvisTheme.text
        font.family: JarvisTheme.mono
        font.pixelSize: 13
        font.bold: button.primary
        font.letterSpacing: 1
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 3
        color: !button.enabled ? "transparent"
             : button.down ? Qt.rgba(button.tint.r, button.tint.g, button.tint.b, 0.3)
             : button.on || button.primary || button.danger ? Qt.rgba(button.tint.r, button.tint.g, button.tint.b, button.hovered ? 0.26 : 0.16)
             : button.hovered ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0.01, 0.04, 0.08, 0.7)
        border.width: button.visualFocus ? 2 : 1
        border.color: !button.enabled ? JarvisTheme.line
                    : button.visualFocus || button.hovered || button.on || button.primary || button.danger ? button.tint : JarvisTheme.line
    }
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 450
}
