import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// The HUD between the chat and the memory panel: the core in the middle and
// holographic panels beside it, all fed by real data — the state machine (vs),
// system telemetry, the voice, memory JSON and tasks. Event effects (memory
// network, bursts, notifications) are started by HudStage.event().
Item {
    id: stage
    // Particles and bursts stay inside the stage, never over the chat.
    clip: true

    required property var ctl            // HudController
    required property var vs             // VisualStateController
    required property var chat           // ChatModel
    required property var voice          // VoiceController
    required property var telemetry      // HudTelemetry
    property var graphics: null          // GraphicsInfo: what really renders
    required property var settings       // AppSettings
    property var study: null
    property string asked: ""            // the request being answered
    property bool memoryOpen: false
    property bool overlayOn: false
    property bool demo: false
    signal coreActivated()
    signal toggleMemory()
    signal toggleOverlay()
    signal toggleDemo()
    signal qualityPicked(string quality)

    // Where the core sits, for the background shader.
    readonly property point coreCentre: Qt.point(coreBox.x + coreBox.width / 2, coreBox.y + coreBox.height / 2)
    readonly property real coreRadius: core.size * 0.3

    readonly property var memoryData: { try { return JSON.parse(stage.chat.memory) } catch (e) { return {} } }

    // One real event: the HUD reacts with an effect fitting it.
    function event(kind, detail) {
        if (kind === "route" && (detail === "recall" || detail === "learn" || detail === "memory")) core.memory("recall")
        else if (kind === "memory") { core.memory("write"); core.burst(40) }
        else if (kind === "request") { ctl.fire(0.9); activePanel.touch() }
        else if (kind === "reply") ctl.fire(0.7)
        else if (kind === "tool") { ctl.fire(0.6); activePanel.touch() }
        if (kind === "notice" || kind === "error" || kind === "fallback" || kind === "warning" || (kind === "daemon" && detail === "off")) {
            noticePanel.kind = kind
            noticePanel.text = detail
            noticePanel.touch()
            core.burst(kind === "error" ? 70 : 30)
            ctl.fire(1.0)
        }
    }

    function fmt(v, digits, unit) { return v < 0 ? "—" : v.toFixed(digits) + (unit || "") }
    function routeName(r) { return JarvisTheme.routeName(r) }
    function eventText(e) {
        switch (e.kind) {
        case "request": return e.detail > 0 ? qsTr("Request with %1 file(s)").arg(e.detail) : qsTr("Request sent")
        case "route": return qsTr("Route: %1").arg(routeName(e.detail))
        case "route-ext": return qsTr("Another client: %1").arg(routeName(e.detail))
        case "stream": return qsTr("Answer streaming")
        case "reply": return qsTr("Answer, %1 characters").arg(e.detail)
        case "error": return qsTr("Error: %1").arg(e.detail)
        case "fallback": return qsTr("Background service failed, answering locally")
        case "notice": return qsTr("Notification: %1").arg(e.detail)
        case "memory": return qsTr("Memory updated")
        case "daemon": return e.detail === "on" ? qsTr("Background service connected") : qsTr("Background service stopped")
        case "tool": return qsTr("Running: %1").arg(e.detail)
        case "voice":
            return e.detail === "speak" ? qsTr("Speaking") : e.detail === "speak-end" ? qsTr("Finished speaking")
                 : e.detail === "listen" ? qsTr("Listening") : qsTr("Stopped listening")
        case "warning": return qsTr("Warning: %1").arg(e.detail)
        default: return e.kind + " " + e.detail
        }
    }

    // ---- Small widgets ----
    component Meter: Column {
        property string label
        property string value
        property real fraction: -1
        property color tint: JarvisTheme.cyan
        width: parent ? parent.width : 200
        spacing: 3
        Item {
            width: parent.width; height: 14
            Text { text: parent.parent.label; color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10; font.letterSpacing: 1 }
            Text { anchors.right: parent.right; text: parent.parent.value; color: JarvisTheme.text; font.family: JarvisTheme.mono; font.pixelSize: 11; font.bold: true }
        }
        Rectangle {
            visible: parent.fraction >= 0
            width: parent.width; height: 3; color: Qt.rgba(1, 1, 1, 0.08)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, parent.parent.fraction)); height: parent.height
                color: parent.parent.fraction > 0.85 ? JarvisTheme.amber : parent.parent.tint
                // No animation: telemetry changes every second, and every animation
                // frame would redraw the whole HUD at the display's rate.
            }
        }
    }
    component Line: Item {
        property string label
        property string value
        property color tint: JarvisTheme.text
        width: parent ? parent.width : 200
        height: 16
        Text { id: lineLabel; text: parent.label; color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
        Text { anchors.right: parent.right; width: parent.width - lineLabel.implicitWidth - 8; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; text: parent.value; color: parent.tint; font.family: JarvisTheme.mono; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
    }

    // ---- Top bar: state, clock and the HUD's controls ----
    component Chip: Rectangle {
        id: chip
        property string text
        property bool on: false
        property string tip
        signal clicked()
        implicitWidth: chipText.implicitWidth + 18
        implicitHeight: 24
        radius: 3
        color: on ? Qt.rgba(stage.ctl.colorA.r, stage.ctl.colorA.g, stage.ctl.colorA.b, 0.22) : chipHover.hovered ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
        border.color: on || chipHover.hovered ? stage.ctl.colorA : JarvisTheme.line
        Text { id: chipText; anchors.centerIn: parent; text: chip.text; color: chip.on ? JarvisTheme.ice : JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10; font.letterSpacing: 1 }
        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: chip.clicked() }
        ToolTip.visible: chipHover.hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 500
    }
    Row {
        id: topBar
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        z: 3
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Qt.formatTime(clock.now, "HH:mm:ss")
            color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 11; font.letterSpacing: 2
            Timer { id: clock; property date now: new Date(); interval: 1000; repeat: true; running: stage.visible; onTriggered: now = new Date() }
        }
        Item { width: 6; height: 1 }
        Repeater {
            model: [["low", qsTr("LOW"), qsTr("Low: fewer rings, no particles, 20 frames per second")],
                    ["balanced", qsTr("BALANCED"), qsTr("Balanced: every layer without the background haze, 30 frames per second while idle, 60 when active")],
                    ["cinematic", qsTr("CINEMATIC"), qsTr("Cinematic: every effect, 60 frames per second")]]
            Chip {
                required property var modelData
                text: modelData[1]; tip: modelData[2]
                on: stage.ctl.quality === modelData[0]
                onClicked: stage.qualityPicked(modelData[0])
            }
        }
        Item { width: 6; height: 1 }
        Chip { text: qsTr("DEMO"); on: stage.demo; tip: qsTr("Demonstration: made-up events show every effect. They are marked DEMO and never counted as real activity."); onClicked: stage.toggleDemo() }
        Chip { text: "◱"; on: stage.overlayOn; tip: qsTr("Compact overlay: a small core above other windows · Ctrl+Shift+O"); onClicked: stage.toggleOverlay() }
        Chip { text: stage.memoryOpen ? qsTr("MEMORY ▸") : qsTr("◂ MEMORY"); on: stage.memoryOpen; tip: qsTr("Show or hide the memory panel · Ctrl+M"); onClicked: stage.toggleMemory() }
    }

    // ---- The core ----
    Item {
        id: coreBox
        // Panels need about 200 px on each side; the core takes what is left.
        readonly property real side: Math.max(220, Math.min(stage.height * 0.9 - 30, stage.width - 2 * 214))
        width: side
        height: side
        x: (stage.width - side) / 2
        y: (stage.height - side) / 2 + 14
        JarvisCore {
            id: core
            anchors.fill: parent
            ctl: stage.ctl
            vs: stage.vs
            graph: stage.chat.graph
            cpu: stage.telemetry.cpu
            ram: stage.telemetry.ram
            onActivated: stage.coreActivated()
        }
    }

    // ---- Panels ----
    HUDPanelManager {
        id: panels
        anchors.fill: parent
        anchors.topMargin: 30
        coreRect: Qt.rect(coreBox.x, coreBox.y, coreBox.width, coreBox.height)

        HolographicPanel {
            column: "left"; order: 0
            title: qsTr("SYSTEM")
            tag: stage.telemetry.cores + " CPU"
            Column {
                width: parent.width; spacing: 6
                Meter { label: qsTr("CPU"); value: stage.fmt(stage.telemetry.cpu, 0, " %"); fraction: stage.telemetry.cpu / 100 }
                Row {
                    id: cpuHistory
                    width: parent.width; height: 18; spacing: 1
                    clip: true
                    Repeater {
                        // As many recent samples as fit the panel (bars are 4 px + 1 px apart).
                        model: stage.telemetry.cpuHistory.slice(-Math.max(1, Math.floor(cpuHistory.width / 5)))
                        Rectangle {
                            required property real modelData
                            anchors.bottom: parent.bottom
                            width: 4; height: Math.max(1, 18 * modelData / 100)
                            color: JarvisTheme.cyan; opacity: 0.25 + 0.6 * modelData / 100
                        }
                    }
                }
                Meter { label: qsTr("RAM"); value: stage.telemetry.ramTotalGb > 0 ? qsTr("%1 / %2 GB").arg(stage.telemetry.ramUsedGb.toFixed(1)).arg(stage.telemetry.ramTotalGb.toFixed(0)) : "—"; fraction: stage.telemetry.ram / 100 }
                // Every GPU found; the one that draws Jarvis is marked ◆ and comes first.
                Repeater {
                    model: stage.telemetry.gpus.filter(g => g.rendering).concat(stage.telemetry.gpus.filter(g => !g.rendering))
                    Meter {
                        required property var modelData
                        label: (modelData.rendering ? "◆ " : "") + modelData.name.replace("NVIDIA GeForce ", "").replace(" Laptop GPU", "").replace(" Mobile", "")
                        value: modelData.asleep ? qsTr("asleep") : stage.fmt(modelData.load, 0, " %")
                        fraction: modelData.load >= 0 ? modelData.load / 100 : -1
                        tint: modelData.rendering ? JarvisTheme.cyan : JarvisTheme.textDim
                    }
                }
                Meter { visible: stage.telemetry.gpuMemory >= 0; label: qsTr("VRAM"); value: stage.fmt(stage.telemetry.gpuMemory, 0, " %"); fraction: stage.telemetry.gpuMemory / 100 }
                Text {
                    visible: !!stage.graphics && stage.graphics.ready && stage.graphics.software
                    width: parent.width
                    text: qsTr("Software rendering: the CPU draws the HUD. See Control Center → Application.")
                    color: JarvisTheme.red; font.pixelSize: 11; wrapMode: Text.Wrap
                }
            }
            details: Column {
                width: parent.width; spacing: 2
                Line { label: qsTr("Temperature"); value: stage.fmt(stage.telemetry.temperature, 0, " °C") }
                Line { label: qsTr("HUD frames/s"); value: stage.fmt(stage.telemetry.fps, 0) }
                Line {
                    label: qsTr("Renderer")
                    value: stage.graphics && stage.graphics.ready ? stage.graphics.api + " · " + stage.graphics.renderingName : "—"
                    tint: stage.graphics && stage.graphics.software ? JarvisTheme.red : JarvisTheme.text
                }
                Line { label: qsTr("Quality"); value: stage.ctl.quality }
            }
        }

        HolographicPanel {
            column: "left"; order: 1
            title: qsTr("ASSISTANT")
            tag: stage.chat.daemonConnected ? "jarvisd" : qsTr("local")
            busy: stage.chat.busy
            accent: JarvisTheme.stateColor(stage.vs.state)
            Column {
                width: parent.width; spacing: 2
                Line { label: qsTr("State"); value: JarvisTheme.stateName(stage.vs.state); tint: JarvisTheme.stateColor(stage.vs.state) }
                Line { label: qsTr("Model"); value: stage.settings.hasKey ? stage.settings.model : qsTr("no Claude key") }
                Line { label: qsTr("Route"); value: stage.vs.routes.length > 0 ? stage.vs.routes.map(stage.routeName).join(" › ") : "—" }
                Line { label: stage.vs.streaming ? qsTr("Stream") : qsTr("Last answer"); value: stage.vs.streaming ? qsTr("%1 ch/s · %2").arg(Math.round(stage.vs.streamRate)).arg(stage.vs.streamChars) : (stage.vs.lastLatency > 0 ? qsTr("%1 s").arg((stage.vs.lastLatency / 1000).toFixed(1)) : "—") }
            }
            details: Column {
                width: parent.width; spacing: 2
                Line { label: qsTr("Requests"); value: String(stage.vs.counters.requests) }
                Line { label: qsTr("Claude calls"); value: String(stage.vs.counters.llm) }
                Line { label: qsTr("Memory recalls"); value: String(stage.vs.counters.recall) }
                Line { label: qsTr("Errors"); value: String(stage.vs.counters.errors) }
            }
        }

        HolographicPanel {
            id: tasksPanel
            column: "left"; order: 2
            readonly property var upcoming: (stage.memoryData.tasks || []).filter(t => !t.done && t.due > 0).sort((a, b) => a.due - b.due).slice(0, 4)
            readonly property int open: (stage.memoryData.tasks || []).filter(t => !t.done).length
            wantShown: open > 0
            title: qsTr("TASKS")
            tag: String(open)
            Column {
                width: parent.width; spacing: 2
                Repeater {
                    model: tasksPanel.upcoming
                    Line { required property var modelData; label: modelData.when || ""; value: modelData.text }
                }
                Text { visible: tasksPanel.upcoming.length === 0; text: qsTr("%1 open, none with a time").arg(tasksPanel.open); color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10 }
            }
        }

        // Transient: the request being answered and background jobs.
        HolographicPanel {
            id: activePanel
            column: "right"; order: -2
            autoHide: 4000
            wantShown: false
            origin: stage.coreCentre
            busy: stage.chat.busy || jobs.length > 0
            readonly property var jobs: {
                const out = []
                if (stage.voice.busy && !stage.voice.recording) out.push(stage.voice.status)
                if (stage.study && stage.study.busy) out.push(stage.study.status)
                return out
            }
            onBusyChanged: if (busy) touch()
            title: stage.chat.busy ? qsTr("ACTIVE REQUEST") : qsTr("ACTIVITY")
            // Time since the request was sent, re-read every tenth of a second of the HUD clock.
            function age(tenth) { return stage.vs.requestAge > 0 ? (stage.vs.requestAge / 1000).toFixed(1) + " s" : "" }
            tag: stage.chat.busy ? age(Math.floor(stage.ctl.time * 10)) : ""
            Column {
                width: parent.width; spacing: 4
                Text {
                    visible: stage.asked.length > 0
                    width: parent.width
                    text: "› " + stage.asked
                    color: JarvisTheme.text; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                }
                Line { visible: stage.vs.routes.length > 0; label: qsTr("Route"); value: stage.vs.routes.map(stage.routeName).join(" › ") }
                Line { visible: stage.vs.streaming; label: qsTr("Stream"); value: qsTr("%1 characters").arg(stage.vs.streamChars) }
                Repeater {
                    model: activePanel.jobs
                    Text { required property string modelData; width: parent.width; text: "◌ " + modelData; color: JarvisTheme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                }
            }
        }

        // Transient: notifications, warnings and errors emerge from the core.
        HolographicPanel {
            id: noticePanel
            column: "right"; order: -1
            autoHide: 5500
            wantShown: false
            origin: stage.coreCentre
            property string kind: "notice"
            property string text
            accent: kind === "error" ? JarvisTheme.red : kind === "notice" ? JarvisTheme.ice : JarvisTheme.amber
            title: kind === "error" ? qsTr("ERROR") : kind === "notice" ? qsTr("NOTIFICATION") : qsTr("WARNING")
            Text {
                width: parent.width
                text: noticePanel.kind === "fallback" ? qsTr("The background service did not answer; this window answers by itself.")
                    : noticePanel.kind === "daemon" ? qsTr("The background service stopped. Reminders pause until it is back.")
                    : noticePanel.text
                color: JarvisTheme.text; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
            }
        }

        HolographicPanel {
            column: "right"; order: 1
            title: qsTr("VOICE")
            tag: stage.voice.enabled ? stage.voice.activeEngine : qsTr("off")
            busy: stage.voice.speaking || stage.voice.recording
            Column {
                width: parent.width; spacing: 5
                Line { label: qsTr("Now"); value: stage.voice.speaking ? qsTr("speaking") : stage.voice.recording ? qsTr("listening") : qsTr("quiet") }
                Meter {
                    label: qsTr("Output"); fraction: stage.voice.outputLevel
                    value: stage.voice.outputMeasured ? Math.round(stage.voice.outputLevel * 100) + " %" : (stage.voice.speaking ? qsTr("not measured") : "—")
                }
                Row {
                    spacing: 4
                    visible: stage.voice.outputMeasured
                    Repeater {
                        model: [qsTr("LOW"), qsTr("MID"), qsTr("HIGH")]
                        Column {
                            required property int index
                            required property string modelData
                            spacing: 2
                            Rectangle { width: 62; height: 18; color: Qt.rgba(1, 1, 1, 0.06)
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height * (stage.voice.outputBands[index] || 0); color: JarvisTheme.cyan; opacity: 0.8 } }
                            Text { text: modelData; color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 9 }
                        }
                    }
                }
                Meter { label: qsTr("Microphone"); fraction: stage.voice.inputLevel; value: stage.voice.recording ? Math.round(stage.voice.inputLevel * 100) + " %" : "—" }
            }
            details: Text { width: parent.width; text: stage.voice.status; color: JarvisTheme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
        }

        HolographicPanel {
            column: "right"; order: 2
            title: qsTr("MEMORY")
            tag: qsTr("%1 writes").arg(stage.vs.counters.memory)
            readonly property var facts: stage.memoryData.facts || []
            Column {
                width: parent.width; spacing: 2
                Line { label: qsTr("Facts"); value: String((stage.memoryData.facts || []).length) }
                Line { label: qsTr("Taught answers"); value: String(stage.memoryData.examples || 0) }
                Line { label: qsTr("Conversations"); value: String((stage.memoryData.episodes || []).length) }
                Line { label: qsTr("Topics"); value: String((stage.memoryData.topics || []).length) }
            }
            details: Column {
                width: parent.width; spacing: 2
                Repeater {
                    model: (stage.memoryData.facts || []).slice(-4).reverse()
                    Text { required property var modelData; width: parent.width; text: "· " + modelData.value; color: JarvisTheme.textDim; font.pixelSize: 11; elide: Text.ElideRight }
                }
            }
        }

        HolographicPanel {
            column: "right"; order: 3
            title: qsTr("EVENT STREAM")
            tag: stage.vs.demo ? "DEMO" : qsTr("live")
            Column {
                width: parent.width; spacing: 2
                Repeater {
                    model: stage.vs.log.slice(0, 7)
                    Row {
                        id: entry
                        required property var modelData
                        required property int index
                        width: parent.width
                        spacing: 6
                        opacity: 1 - index * 0.1
                        Text { id: when; text: entry.modelData.time; color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10 }
                        Text { id: mark; text: entry.modelData.demo ? "DEMO" : "▸"; color: entry.modelData.demo ? JarvisTheme.amber : JarvisTheme.cyan; font.family: JarvisTheme.mono; font.pixelSize: 10 }
                        Text { width: Math.max(0, entry.width - when.width - mark.width - 2 * entry.spacing); text: stage.eventText(entry.modelData); color: entry.modelData.kind === "error" ? JarvisTheme.red : JarvisTheme.text; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                }
                Text { visible: stage.vs.log.length === 0; text: qsTr("Waiting for activity…"); color: JarvisTheme.textDim; font.pixelSize: 11 }
            }
        }
    }
}
