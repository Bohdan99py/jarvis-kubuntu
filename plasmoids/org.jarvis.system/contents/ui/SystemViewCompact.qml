import QtQuick
import org.jarvis.visual as JV

// In a panel: CPU and RAM as two short numbers.
Row {
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    spacing: 6
    Text {
        text: "CPU " + (telemetry.cpu && telemetry.cpu.usage >= 0 ? Math.round(telemetry.cpu.usage) + "%" : "—")
        color: JV.JarvisTheme.cyan; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
    }
    Text {
        text: "RAM " + (telemetry.memory && telemetry.memory.percent >= 0 ? Math.round(telemetry.memory.percent) + "%" : "—")
        color: JV.JarvisTheme.textSoft; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
    }
}
