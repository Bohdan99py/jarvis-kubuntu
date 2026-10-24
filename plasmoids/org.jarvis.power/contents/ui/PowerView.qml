import QtQuick
import QtQuick.Layouts
import org.jarvis.visual as JV

// Battery (charge, charging, time left when the estimate is sound), mains,
// the power draw, and the power profile (power-profiles-daemon) with its switch.
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)
    implicitHeight: panel.height

    readonly property var power: telemetry.power || {}
    function duration(minutes) {
        if (minutes === undefined || minutes < 0) return ""
        const h = Math.floor(minutes / 60), m = minutes % 60
        return h > 0 ? i18nd("jarvis-desktop", "%1 h %2 min", h, m) : i18nd("jarvis-desktop", "%1 min", m)
    }
    readonly property string stateText: !power.present ? i18nd("jarvis-desktop", "No battery · mains power")
        : power.charging ? i18nd("jarvis-desktop", "Charging") + (power.minutesLeft >= 0 ? " · " + i18nd("jarvis-desktop", "full in %1", duration(power.minutesLeft)) : "")
        : power.status === "Discharging" ? i18nd("jarvis-desktop", "On battery") + (power.minutesLeft >= 0 ? " · " + i18nd("jarvis-desktop", "%1 left", duration(power.minutesLeft)) : "")
        : power.onAc ? i18nd("jarvis-desktop", "On mains power") : (power.status || "")
    function profileName(p) {
        switch (p) {
        case "power-saver": return i18nd("jarvis-desktop", "Saver")
        case "balanced": return i18nd("jarvis-desktop", "Balanced")
        case "performance": return i18nd("jarvis-desktop", "Performance")
        default: return p || ""
        }
    }

    JV.HolographicPanel {
        id: panel
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "POWER")
        tag: view.profileName(view.power.profile).toUpperCase()
        accent: view.power.powerSaver ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
        shown: true
        movable: false
        Column {
            width: parent.width
            spacing: Math.round(5 * JV.JarvisTheme.spacing)
            Text {
                visible: !view.online
                text: i18nd("jarvis-desktop", "Jarvis is not running: no measurements.")
                color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall + 1
            }
            // The charge, large, with the state beside it.
            RowLayout {
                visible: view.online
                width: parent.width
                spacing: 12
                Text {
                    text: view.power.present ? Math.round(view.power.percent) + "%" : "AC"
                    color: view.power.charging ? JV.JarvisTheme.positive : view.power.percent <= 15 ? JV.JarvisTheme.red : JV.JarvisTheme.text
                    font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontLarge + 6; font.bold: true
                }
                Column {
                    Layout.fillWidth: true
                    Text { width: parent.width; text: (view.power.charging ? "⚡ " : "") + view.stateText; color: JV.JarvisTheme.text; font.pixelSize: JV.JarvisTheme.fontSmall + 1; wrapMode: Text.Wrap }
                    Text {
                        visible: (view.power.watts || -1) > 0
                        text: visible ? i18nd("jarvis-desktop", "%1 W", view.power.watts) : ""
                        color: JV.JarvisTheme.textSoft; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
                    }
                }
            }
            JV.HoloMeter {
                width: parent.width
                visible: !!view.power.present
                label: i18nd("jarvis-desktop", "Battery")
                value: view.power.percent !== undefined ? view.power.percent : -1
                tint: view.power.charging ? JV.JarvisTheme.positive : JV.JarvisTheme.cyan
                warnAt: 101
                animated: view.effects
            }
            Row {
                visible: view.extra && view.online && !!view.power.profile
                spacing: 6
                Repeater {
                    model: ["power-saver", "balanced", "performance"]
                    JV.HoloButton {
                        required property string modelData
                        text: view.profileName(modelData)
                        checked: view.power.profile === modelData
                        onClicked: view.action("profile", modelData)
                    }
                }
            }
            Text {
                visible: view.online && view.power.profile === ""
                text: i18nd("jarvis-desktop", "Power profiles: not available")
                color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall
            }
        }
    }
}
