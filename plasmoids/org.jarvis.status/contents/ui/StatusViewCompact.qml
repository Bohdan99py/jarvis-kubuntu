import QtQuick
import org.jarvis.visual as JV

Row {
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    spacing: 4
    Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: JV.JarvisTheme.stateColor(online ? (status.state || "idle") : "offline") }
    Text { text: online ? JV.JarvisTheme.stateName(status.state || "idle").toUpperCase() : "OFFLINE"; color: JV.JarvisTheme.textDim; font.family: JV.JarvisTheme.mono; font.pixelSize: 10 }
}
