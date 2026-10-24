import QtQuick
import QtQuick.Layouts
import org.jarvis.visual as JV

// CPU, memory, every GPU (load, VRAM, temperature, clock; a sleeping NVIDIA
// card is shown asleep, not woken up), temperatures and fans — what jarvisd
// measured, "—" for what it could not. 0.19.3: a short activity graph for the
// CPU, the memory and each GPU (the last minute, kept here from the samples
// jarvisd sends — no extra measuring), the hottest sensors as chips, and the
// details (every core, clocks, fans) behind the title (click it).
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
    implicitHeight: panel.height

    // The last 30 samples of memory and of each GPU's load.
    property var memHistory: []
    property var gpuHistory: ({})
    function push(list, v) { const l = (list || []).concat([v]); return l.length > 30 ? l.slice(l.length - 30) : l }
    onTelemetryChanged: {
        if (mem.percent !== undefined && mem.percent >= 0) memHistory = push(memHistory, mem.percent)
        const g = Object.assign({}, gpuHistory)
        for (const card of gpus)
            if (card.load !== undefined && card.load >= 0) g[card.name] = push(g[card.name], card.load)
        gpuHistory = g
    }
    function temp(c) { return c === undefined || c <= 0 ? "" : Math.round(c) + " °C" }
    function shortGpu(name) { return String(name || "").replace(/^(NVIDIA|AMD|Intel\(R\))\s+/, "").replace(/\s+(Laptop GPU|Graphics)$/, "") }

    component Graph: JV.Sparkline {
        width: parent.width
        height: Math.round(22 * JV.JarvisTheme.spacing)
        maximum: 100
    }
    component Chip: Rectangle {
        property alias text: chipText.text
        property color tint: JV.JarvisTheme.textSoft
        width: chipText.implicitWidth + 14
        height: chipText.implicitHeight + 6
        radius: height / 2
        color: Qt.rgba(tint.r, tint.g, tint.b, 0.1)
        border.color: Qt.rgba(tint.r, tint.g, tint.b, 0.35)
        Text { id: chipText; anchors.centerIn: parent; color: parent.tint; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall }
    }

    JV.HolographicPanel {
        id: panel
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "SYSTEM")
        tag: view.cpu.cores ? i18nd("jarvis-desktop", "%1 CPU", view.cpu.cores) : ""
        shown: true
        movable: false
        busy: view.effects && (view.cpu.usage || 0) > 85

        Column {
            width: parent.width
            spacing: Math.round(5 * JV.JarvisTheme.spacing)
            Text {
                visible: !view.online
                width: parent.width
                text: i18nd("jarvis-desktop", "Jarvis is not running: no measurements.")
                color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall + 1; wrapMode: Text.Wrap
            }
            JV.HoloMeter { width: parent.width; label: i18nd("jarvis-desktop", "CPU"); value: view.cpu.usage !== undefined ? view.cpu.usage : -1; animated: view.effects }
            Graph { visible: view.extra; values: view.cpu.history || [] }
            JV.HoloMeter {
                width: parent.width
                label: i18nd("jarvis-desktop", "RAM")
                value: view.mem.percent !== undefined ? view.mem.percent : -1
                valueText: view.mem.totalGb ? view.mem.usedGb + "/" + view.mem.totalGb + " GB" : "—"
                tint: JV.JarvisTheme.ice
                animated: view.effects
            }
            Graph { visible: view.extra; values: view.memHistory; color: JV.JarvisTheme.ice; fill: 0.08 }
            // Count models: a delegate lives as long as its card (a JS array as the model
            // rebuilt every delegate on every sample — six bars re-animating from zero).
            Repeater {
                model: view.gpus.length
                Column {
                    required property int index
                    readonly property var modelData: view.gpus[index] || ({})
                    width: parent.width
                    spacing: 3
                    RowLayout {
                        width: parent.width
                        Text {
                            Layout.fillWidth: true
                            text: view.shortGpu(modelData.name) + (modelData.asleep ? "  · " + i18nd("jarvis-desktop", "asleep") : "")
                            color: JV.JarvisTheme.text; font.pixelSize: JV.JarvisTheme.fontSmall + 1; elide: Text.ElideRight
                        }
                        Text {
                            text: [view.temp(modelData.temperature), modelData.clockMhz > 0 ? Math.round(modelData.clockMhz) + " MHz" : ""].filter(x => x).join(" · ")
                            color: modelData.temperature >= 85 ? JV.JarvisTheme.amber : JV.JarvisTheme.textSoft
                            font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
                        }
                    }
                    JV.HoloMeter { width: parent.width; label: i18nd("jarvis-desktop", "GPU"); value: modelData.load; animated: view.effects }
                    JV.HoloMeter {
                        width: parent.width
                        visible: modelData.vramTotalMb > 0
                        label: i18nd("jarvis-desktop", "VRAM")
                        value: modelData.memory
                        valueText: modelData.vramTotalMb > 0 ? (modelData.vramUsedMb / 1024).toFixed(1) + "/" + (modelData.vramTotalMb / 1024).toFixed(1) + " GB" : "—"
                        tint: JV.JarvisTheme.ice
                        animated: view.effects
                    }
                    Graph { visible: view.extra && !modelData.asleep; values: view.gpuHistory[modelData.name] || []; height: Math.round(16 * JV.JarvisTheme.spacing) }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                visible: view.hottest.length > 0
                Repeater {
                    model: view.hottest.length
                    Chip {
                        required property int index
                        readonly property var modelData: view.hottest[index] || ({})
                        text: modelData.chip + " " + Math.round(modelData.celsius) + "°"
                        tint: modelData.celsius >= 85 ? JV.JarvisTheme.amber : JV.JarvisTheme.textSoft
                    }
                }
            }
        }

        details: Column {
            width: parent ? parent.width : 0
            spacing: 5
            Text {
                width: parent.width
                visible: (view.cpu.mhzAverage || -1) > 0
                text: visible ? i18nd("jarvis-desktop", "Clock %1 MHz · max %2 MHz · load %3", view.cpu.mhzAverage, view.cpu.mhzMax, view.cpu.load) : ""
                color: JV.JarvisTheme.textSoft; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
                elide: Text.ElideRight
            }
            // Every core as a thin column.
            Row {
                visible: (view.cpu.perCore || []).length > 0
                width: parent.width
                height: 28
                spacing: 2
                Repeater {
                    model: (view.cpu.perCore || []).length
                    Rectangle {
                        required property int index
                        readonly property real modelData: (view.cpu.perCore || [])[index] || 0
                        width: (parent.width - 2 * ((view.cpu.perCore || []).length - 1)) / Math.max(1, (view.cpu.perCore || []).length)
                        height: 28
                        radius: 2
                        color: Qt.rgba(1, 1, 1, 0.06)
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            radius: 2
                            height: parent.height * Math.max(0, Math.min(100, modelData)) / 100
                            color: modelData > 85 ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
                            opacity: 0.85
                        }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: (view.thermal.fans || []).length
                    Chip {
                        required property int index
                        readonly property var modelData: (view.thermal.fans || [])[index] || ({})
                        text: "⟳ " + modelData.label + " " + modelData.rpm + " rpm"
                    }
                }
                Text {
                    visible: view.online && view.thermal.sensors !== undefined && (view.thermal.fans || []).length === 0
                    text: i18nd("jarvis-desktop", "no fan sensors")
                    color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall
                }
            }
        }
    }
}
