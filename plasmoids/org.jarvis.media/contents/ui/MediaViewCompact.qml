import QtQuick
import org.jarvis.visual as JV

Text {
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    readonly property var media: telemetry.media || {}
    width: Math.min(implicitWidth, 200)
    text: media.available ? (media.status === "Playing" ? "▶ " : "⏸ ") + (media.title || "") : "♪"
    color: JV.JarvisTheme.cyan; font.pixelSize: JV.JarvisTheme.fontSmall; elide: Text.ElideRight
}
