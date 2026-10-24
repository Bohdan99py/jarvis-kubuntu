import QtQuick
import org.jarvis.visual as JV

// Download and upload speed with a short history, the connection (Wi-Fi,
// Ethernet), VPN — from /proc/net and NetworkManager through jarvisd.
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)

    readonly property var net: telemetry.net || {}
    function rate(bytes) {
        if (bytes === undefined || bytes < 0) return "—"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB/s"
        if (bytes >= 1024) return Math.round(bytes / 1024) + " KB/s"
        return Math.round(bytes) + " B/s"
    }

    JV.HolographicPanel {
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "NETWORK")
        tag: view.net.vpn ? "VPN" : (view.net.type || "").toUpperCase()
        accent: view.net.vpn ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
        shown: true
        movable: false
        Column {
            width: parent.width
            spacing: 4 * JV.JarvisTheme.spacing
            Text {
                width: parent.width
                text: !view.online ? i18nd("jarvis-desktop", "Jarvis is not running: no measurements.")
                    : view.net.name ? view.net.name + (view.net.interface ? "  ·  " + view.net.interface : "")
                    : view.net.interface ? view.net.interface
                    : i18nd("jarvis-desktop", "No connection")
                color: JV.JarvisTheme.text; font.pixelSize: 12; elide: Text.ElideRight
            }
            Row {
                spacing: 16
                Text { text: "↓ " + view.rate(view.net.rxBytesPerSecond); color: JV.JarvisTheme.cyan; font.family: JV.JarvisTheme.mono; font.pixelSize: 13; font.bold: true }
                Text { text: "↑ " + view.rate(view.net.txBytesPerSecond); color: JV.JarvisTheme.amber; font.family: JV.JarvisTheme.mono; font.pixelSize: 13; font.bold: true }
            }
            Item {
                visible: view.extra
                width: parent.width
                height: 36
                JV.Sparkline { anchors.fill: parent; values: view.net.rxHistory || []; color: JV.JarvisTheme.cyan }
                JV.Sparkline { anchors.fill: parent; values: view.net.txHistory || []; color: JV.JarvisTheme.amber; fill: 0.05
                               maximum: Math.max(...(view.net.rxHistory && view.net.rxHistory.length ? view.net.rxHistory : [1])) }
            }
            Text {
                width: parent.width
                visible: view.net.manager !== undefined
                text: view.net.vpn ? i18nd("jarvis-desktop", "VPN on: %1", view.net.vpnName || "VPN") : i18nd("jarvis-desktop", "VPN off")
                color: view.net.vpn ? JV.JarvisTheme.amber : JV.JarvisTheme.textDim; font.pixelSize: 11
            }
        }
    }
}
