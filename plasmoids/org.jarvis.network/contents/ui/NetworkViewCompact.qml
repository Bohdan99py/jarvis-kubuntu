import QtQuick
import org.jarvis.visual as JV

Row {
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    readonly property var net: telemetry.net || {}
    function kb(v) { return v === undefined || v < 0 ? "—" : Math.round(v / 1024) + "K" }
    spacing: 6
    Text { text: "↓" + kb(net.rxBytesPerSecond); color: JV.JarvisTheme.cyan; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall }
    Text { text: "↑" + kb(net.txBytesPerSecond); color: JV.JarvisTheme.amber; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall }
    Text { visible: !!net.vpn; text: "VPN"; color: JV.JarvisTheme.amber; font.pixelSize: JV.JarvisTheme.fontSmall }
}
