import QtQuick
import QtQuick.Layouts
import org.jarvis.visual as JV

// Download and upload with the last minute's graph, the active connection
// (Wi-Fi, Ethernet) and VPN on their own lines — from /proc/net and
// NetworkManager through jarvisd. Nothing here changes the network: that is
// Plasma's network applet in the dock's tray.
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)
    implicitHeight: panel.height

    readonly property var net: telemetry.net || {}
    function rate(bytes) {
        if (bytes === undefined || bytes < 0) return "—"
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB/s"
        if (bytes >= 1024) return Math.round(bytes / 1024) + " KB/s"
        return Math.round(bytes) + " B/s"
    }
    function kind(type) {
        switch (type) {
        case "wifi": return i18nd("jarvis-desktop", "Wi-Fi")
        case "ethernet": return i18nd("jarvis-desktop", "Ethernet")
        default: return type ? String(type) : ""
        }
    }

    JV.HolographicPanel {
        id: panel
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "NETWORK")
        tag: view.kind(view.net.type).toUpperCase()
        accent: view.net.vpn ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
        shown: true
        movable: false
        Column {
            width: parent.width
            spacing: Math.round(5 * JV.JarvisTheme.spacing)
            // The connection.
            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: view.net.type === "wifi" ? "◉" : view.net.interface ? "⇄" : "○"; color: JV.JarvisTheme.cyan; font.pixelSize: JV.JarvisTheme.fontBody }
                Text {
                    Layout.fillWidth: true
                    text: !view.online ? i18nd("jarvis-desktop", "Jarvis is not running: no measurements.")
                        : view.net.name ? view.net.name
                        : view.net.interface ? view.net.interface
                        : i18nd("jarvis-desktop", "No connection")
                    color: JV.JarvisTheme.text; font.pixelSize: JV.JarvisTheme.fontBody; font.bold: !!view.net.name; elide: Text.ElideRight
                }
                Text {
                    visible: !!view.net.interface && !!view.net.name
                    text: view.net.interface || ""
                    color: JV.JarvisTheme.textSoft; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
                }
            }
            // The two directions, each with its graph.
            Row {
                width: parent.width
                spacing: 12
                Repeater {
                    model: 2   // (a count: the columns stay, only their numbers change)
                    Column {
                        required property int index
                        readonly property var modelData: index === 0
                            ? { arrow: "↓", label: i18nd("jarvis-desktop", "Download"), value: view.net.rxBytesPerSecond, history: view.net.rxHistory || [], tint: JV.JarvisTheme.cyan }
                            : { arrow: "↑", label: i18nd("jarvis-desktop", "Upload"), value: view.net.txBytesPerSecond, history: view.net.txHistory || [], tint: JV.JarvisTheme.amber }
                        width: (parent.width - 12) / 2
                        spacing: 2
                        Text { text: modelData.label; color: JV.JarvisTheme.textSoft; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall }
                        Text { text: modelData.arrow + " " + view.rate(modelData.value); color: modelData.tint; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontBody + 1; font.bold: true }
                        JV.Sparkline { visible: view.extra; width: parent.width; height: Math.round(26 * JV.JarvisTheme.spacing); values: modelData.history; color: modelData.tint }
                    }
                }
            }
            // VPN.
            Rectangle {
                visible: view.net.manager !== undefined
                width: vpnText.implicitWidth + 20
                height: vpnText.implicitHeight + 8
                radius: height / 2
                color: view.net.vpn ? JV.JarvisTheme.alpha(JV.JarvisTheme.amber, 0.14) : "transparent"
                border.color: view.net.vpn ? JV.JarvisTheme.amber : JV.JarvisTheme.edge
                Text {
                    id: vpnText
                    anchors.centerIn: parent
                    text: view.net.vpn ? "🔒 " + i18nd("jarvis-desktop", "VPN on: %1", view.net.vpnName || "VPN") : i18nd("jarvis-desktop", "VPN off")
                    color: view.net.vpn ? JV.JarvisTheme.amber : JV.JarvisTheme.textSoft
                    font.pixelSize: JV.JarvisTheme.fontSmall + 1
                }
            }
        }
    }
}
