import QtQuick

// A labelled bar for the holographic widgets: "CPU ▮▮▮▯▯ 42 %". -1 = unknown ("—").
Item {
    id: meter
    property string label
    property real value: -1          // 0..100
    property string valueText: value < 0 ? "—" : Math.round(value) + " %"
    property real warnAt: 85
    property bool animated: true
    implicitHeight: Math.round(18 * JarvisTheme.spacing)
    implicitWidth: 200
    Text {
        id: caption
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(96, parent.width * 0.32)
        text: meter.label
        color: JarvisTheme.textDim
        font.family: JarvisTheme.mono; font.pixelSize: 11
        elide: Text.ElideRight
    }
    Rectangle {
        id: track
        anchors.left: caption.right; anchors.leftMargin: 6
        anchors.right: number.left; anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        height: 4; radius: 2
        color: Qt.rgba(1, 1, 1, 0.07)
        Rectangle {
            height: parent.height; radius: 2
            width: parent.width * Math.max(0, Math.min(100, meter.value)) / 100
            color: meter.value >= meter.warnAt ? JarvisTheme.amber : JarvisTheme.cyan
            Behavior on width { enabled: meter.animated; NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
        }
    }
    Text {
        id: number
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: 64
        horizontalAlignment: Text.AlignRight
        text: meter.valueText
        color: JarvisTheme.text
        font.family: JarvisTheme.mono; font.pixelSize: 11
    }
}
