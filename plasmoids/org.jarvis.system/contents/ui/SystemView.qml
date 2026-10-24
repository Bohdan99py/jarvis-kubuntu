import QtQuick
import QtQuick.Layouts
import org.jarvis.visual as JV

// CPU (all and every core, clocks, history), memory, every GPU (load, VRAM,
// temperature, clock; a sleeping NVIDIA card is shown asleep, not woken up),
// temperatures and fans — what jarvisd measured, "—" for what it could not.
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)

    readonly property var cpu: telemetry.cpu || {}
    readonly property var mem: telemetry.memory || {}
    readonly property var gpus: telemetry.gpu ? telemetry.gpu.cards || [] : []
    readonly property var thermal: telemetry.thermal || {}
    readonly property var hottest: (thermal.sensors || []).slice().sort((a, b) => b.celsius - a.celsius).slice(0, 4)

    JV.HolographicPanel {
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "SYSTEM")
        tag: view.cpu.cores ? i18nd("jarvis-desktop", "%1 CPU", view.cpu.cores) : ""
        shown: true
        movable: false
        busy: view.effects && (view.cpu.usage || 0) > 85

        Column {
            width: parent.width
            spacing: 4 * JV.JarvisTheme.spacing
            Text {
                visible: !view.online
                width: parent.width
                text: i18nd("jarvis-desktop", "Jarvis is not running: no measurements.")
                color: JV.JarvisTheme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            JV.HoloMeter { width: parent.width; label: i18nd("jarvis-desktop", "CPU"); value: view.cpu.usage !== undefined ? view.cpu.usage : -1; animated: view.effects }
            JV.Sparkline { width: parent.width; height: 22; values: view.cpu.history || []; maximum: 100 }
            Text {
                width: parent.width
                visible: (view.cpu.mhzAverage || -1) > 0
                text: i18nd("jarvis-desktop", "Clock %1 MHz · max %2 MHz · load %3", view.cpu.mhzAverage, view.cpu.mhzMax, view.cpu.load)
                color: JV.JarvisTheme.textDim; font.family: JV.JarvisTheme.mono; font.pixelSize: 10
            }
            // Every core as a thin column.
            Row {
                visible: view.extra && (view.cpu.perCore || []).length > 0
                width: parent.width
                height: 26
                spacing: 2
                Repeater {
                    model: view.cpu.perCore || []
                    Rectangle {
                        required property var modelData
                        width: (view.width - 2 * ((view.cpu.perCore || []).length - 1)) / Math.max(1, (view.cpu.perCore || []).length) - 1
                        height: 26
                        color: Qt.rgba(1, 1, 1, 0.06)
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: parent.height * Math.max(0, Math.min(100, modelData)) / 100
                            color: modelData > 85 ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
                            opacity: 0.85
                        }
                    }
                }
            }
            JV.HoloMeter {
                width: parent.width
                label: i18nd("jarvis-desktop", "RAM")
                value: view.mem.percent !== undefined ? view.mem.percent : -1
                valueText: view.mem.totalGb ? view.mem.usedGb + "/" + view.mem.totalGb + " GB" : "—"
                animated: view.effects
            }
            Repeater {
                model: view.gpus
                Column {
                    required property var modelData
                    width: view.width - 20
                    spacing: 2
                    Text {
                        width: parent.width
                        text: modelData.name + (modelData.asleep ? "  · " + i18nd("jarvis-desktop", "asleep") : "")
                              + (modelData.temperature > 0 ? "  · " + Math.round(modelData.temperature) + " °C" : "")
                              + (modelData.clockMhz > 0 ? "  · " + Math.round(modelData.clockMhz) + " MHz" : "")
                        color: JV.JarvisTheme.text; font.pixelSize: 11; elide: Text.ElideRight
                    }
                    JV.HoloMeter { width: parent.width; label: i18nd("jarvis-desktop", "GPU"); value: modelData.load; animated: view.effects }
                    JV.HoloMeter {
                        width: parent.width
                        visible: modelData.vramTotalMb > 0
                        label: i18nd("jarvis-desktop", "VRAM")
                        value: modelData.memory
                        valueText: (modelData.vramUsedMb / 1024).toFixed(1) + "/" + (modelData.vramTotalMb / 1024).toFixed(1) + " GB"
                        animated: view.effects
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 8
                Repeater {
                    model: view.hottest
                    Text {
                        required property var modelData
                        text: modelData.chip + " " + Math.round(modelData.celsius) + "°"
                        color: modelData.celsius >= 85 ? JV.JarvisTheme.amber : JV.JarvisTheme.textDim
                        font.family: JV.JarvisTheme.mono; font.pixelSize: 10
                    }
                }
                Repeater {
                    model: view.thermal.fans || []
                    Text {
                        required property var modelData
                        text: "⟳ " + modelData.label + " " + modelData.rpm + " rpm"
                        color: JV.JarvisTheme.textDim; font.family: JV.JarvisTheme.mono; font.pixelSize: 10
                    }
                }
                Text {
                    visible: view.online && view.thermal.sensors !== undefined && (view.thermal.fans || []).length === 0
                    text: i18nd("jarvis-desktop", "no fan sensors")
                    color: JV.JarvisTheme.textDim; font.pixelSize: 10; opacity: 0.6
                }
            }
        }
    }
}
