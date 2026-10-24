import QtQuick
import org.jarvis.visual as JV

// Battery (charge, charging, time left when the estimate is sound), mains,
// the power profile (power-profiles-daemon) and its switch.
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)

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

    JV.HolographicPanel {
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "POWER")
        tag: view.power.profile ? view.power.profile.toUpperCase() : ""
        accent: view.power.powerSaver ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
        shown: true
        movable: false
        Column {
            width: parent.width
            spacing: 4 * JV.JarvisTheme.spacing
            Text {
                visible: !view.online
                text: i18nd("jarvis-desktop", "Jarvis is not running: no measurements.")
                color: JV.JarvisTheme.textDim; font.pixelSize: 11
            }
            JV.HoloMeter {
                width: parent.width
                visible: !!view.power.present
                label: i18nd("jarvis-desktop", "Battery")
                value: view.power.percent !== undefined ? view.power.percent : -1
                warnAt: 101
                animated: view.effects
            }
            Text { width: parent.width; text: view.stateText; color: JV.JarvisTheme.text; font.pixelSize: 12; wrapMode: Text.Wrap }
            Text {
                visible: (view.power.watts || -1) > 0
                text: i18nd("jarvis-desktop", "%1 W", view.power.watts)
                color: JV.JarvisTheme.textDim; font.family: JV.JarvisTheme.mono; font.pixelSize: 10
            }
            Row {
                visible: view.extra && view.online && !!view.power.profile
                spacing: 4
                Repeater {
                    model: [["power-saver", i18nd("jarvis-desktop", "Saver")], ["balanced", i18nd("jarvis-desktop", "Balanced")], ["performance", i18nd("jarvis-desktop", "Performance")]]
                    JV.HoloButton {
                        required property var modelData
                        text: modelData[1]
                        checked: view.power.profile === modelData[0]
                        onClicked: view.action("profile", modelData[0])
                    }
                }
            }
            Text {
                visible: view.online && view.power.profile === ""
                text: i18nd("jarvis-desktop", "Power profiles: not available")
                color: JV.JarvisTheme.textDim; font.pixelSize: 10; opacity: 0.7
            }
        }
    }
}
