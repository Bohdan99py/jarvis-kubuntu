import QtQuick

// A labelled bar for the holographic widgets: "CPU ▮▮▮▯▯ 42 %". -1 = unknown ("—").
// 0.19.3: the shared type scale (nothing under JarvisTheme.fontSmall), a rounded
// track, the label never covering the number — and only in the cinematic quality,
// only a jump (≥ 15 points) glides:
// a sample every two seconds moving six bars by a percent or two used to run six
// 400 ms animations at the display's rate, about a hundred frames in eight
// seconds for nothing visible (measured in a Plasma session).
Item {
    id: meter
    property real fraction: 0
    function place() {
        const f = Math.max(0, Math.min(100, value)) / 100
        if (animated && JarvisTheme.quality === "cinematic" && Math.abs(f - fraction) >= 0.15) {
            glide.to = f
            glide.restart()
        } else {
            glide.stop()
            fraction = f
        }
    }
    onValueChanged: place()
    Component.onCompleted: fraction = Math.max(0, Math.min(100, value)) / 100
    NumberAnimation { id: glide; target: meter; property: "fraction"; duration: 250; easing.type: Easing.OutCubic }
    property string label
    property real value: -1          // 0..100
    property string valueText: value < 0 ? "—" : Math.round(value) + " %"
    property real warnAt: 85
    property bool animated: true
    property color tint: JarvisTheme.cyan
    implicitHeight: Math.round(20 * JarvisTheme.spacing)
    implicitWidth: 200
    Text {
        id: caption
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(Math.max(48, implicitWidth), parent.width * 0.3)
        text: meter.label
        color: JarvisTheme.textSoft
        font.family: JarvisTheme.mono; font.pixelSize: JarvisTheme.fontSmall
        elide: Text.ElideRight
    }
    Rectangle {
        id: track
        anchors.left: caption.right; anchors.leftMargin: 8
        anchors.right: number.left; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        height: 5; radius: 2.5
        color: Qt.rgba(1, 1, 1, 0.08)
        Rectangle {
            height: parent.height; radius: 2.5
            width: parent.width * meter.fraction
            color: meter.value >= meter.warnAt ? JarvisTheme.amber : meter.tint
        }
    }
    Text {
        id: number
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: Math.max(56, implicitWidth)
        horizontalAlignment: Text.AlignRight
        text: meter.valueText
        color: JarvisTheme.text
        font.family: JarvisTheme.mono; font.pixelSize: JarvisTheme.fontSmall + 1
    }
}
