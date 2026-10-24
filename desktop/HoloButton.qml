import QtQuick

// A flat HUD button: thin border, glow on hover, keyboard and screen-reader friendly.
Rectangle {
    id: button
    property string text
    property string glyph            // a short symbol before the text: ◎ ▶ ⏸
    property bool primary: false
    property bool checked: false
    readonly property alias hovered: mouse.containsMouse
    signal clicked()
    implicitHeight: Math.round(30 * JarvisTheme.spacing)
    implicitWidth: label.implicitWidth + 22
    radius: JarvisTheme.radiusSmall + 1
    color: !enabled ? "transparent"
         : mouse.containsMouse || activeFocus ? Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.22)
         : primary || checked ? Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.12)
         : "transparent"
    border.color: !enabled ? Qt.rgba(1, 1, 1, 0.1) : checked ? JarvisTheme.amber : primary ? JarvisTheme.edgeStrong : JarvisTheme.edge
    opacity: enabled ? 1 : 0.5
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()
    Keys.onReturnPressed: clicked()
    Keys.onSpacePressed: clicked()
    Behavior on color { ColorAnimation { duration: 140 } }
    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 8)
        text: (button.glyph ? button.glyph + (button.text ? "  " : "") : "") + button.text
        color: JarvisTheme.text
        font.pixelSize: JarvisTheme.fontSmall + 1
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
    }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: button.enabled; cursorShape: Qt.PointingHandCursor; onClicked: button.clicked() }
}
