import QtQuick
import org.jarvis.visual as JV

Row {
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    readonly property var power: telemetry.power || {}
    spacing: 4
    Text {
        text: power.present ? (power.charging ? "⚡" : "▮") + " " + Math.round(power.percent) + "%" : "⏻ AC"
        color: power.powerSaver ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
        font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
    }
}
