import QtQuick

// The AI core: independent layers stacked around one centre.
//   analyser ring (measured sound) · rings (own motions) · energy core (shader)
//   · particles · data streams · memory network · readouts with real numbers.
// It scales with its size; everything reads one HudController.
Item {
    id: core
    required property var ctl
    required property var vs          // VisualStateController
    property string graph: "{}"
    property bool compact: false      // the overlay: fewer layers, no text
    property real cpu: -1             // percent, -1 unknown: gauge arcs around the core
    property real ram: -1
    readonly property real size: Math.min(width, height)
    readonly property bool hovered: hover.hovered
    signal activated()

    function memory(mode) { if (!compact) network.flare(mode) }
    function burst(n) { particles.burst(n) }

    Item {
        id: square
        anchors.centerIn: parent
        width: core.size
        height: core.size

        AudioReactiveLayer {
            anchors.fill: parent
            ctl: core.ctl
        }
        CoreRings {
            anchors.fill: parent
            ctl: core.ctl
            count: core.compact ? 3 : (core.ctl.low ? 3 : core.ctl.cinematic ? 8 : 7)
        }
        ParticleField {
            id: particles
            anchors.fill: parent
            ctl: core.ctl
            active: !core.ctl.low && !core.compact
        }
        ShaderEffect {
            id: energy
            anchors.centerIn: parent
            width: parent.width * 0.8
            height: width
            property real time: core.ctl.time
            property real energy: core.ctl.energy + (core.hovered ? 0.12 : 0)
            property real openness: core.ctl.openness
            property real turbulence: core.ctl.turbulence
            property real level: core.ctl.sLevel
            property vector4d bands: Qt.vector4d(core.ctl.sLow, core.ctl.sMid, core.ctl.sHigh, core.ctl.sMic)
            property color colorA: core.ctl.colorA
            property color colorB: core.ctl.colorB
            property color alertColor: core.ctl.alertColor
            property real alertMix: core.ctl.alertMix
            property real instability: core.ctl.instability
            property real pulse: core.ctl.pulse
            property real detail: core.ctl.detail
            fragmentShader: "shaders/core.frag.qsb"
        }
        // Gauges: CPU on the left, memory on the right, as arcs that fill up.
        Repeater {
            // Each fills from the middle of its side outwards, clear of the corner readouts.
            model: core.compact ? [] : [{ v: core.cpu, mid: 0.5 }, { v: core.ram, mid: 0.0 }]
            delegate: ShaderEffect {
                required property var modelData
                anchors.fill: parent
                visible: modelData.v >= 0
                property real time: core.ctl.time
                property real pixel: 2 / Math.max(1, width)
                property real radius: 0.955
                property real thickness: 0.012
                property real segments: 40
                property real duty: 0.7
                readonly property real share: Math.max(0, Math.min(1, modelData.v / 100))
                property real turn: modelData.mid - arcSpan / 2
                property real ticks: 0
                property real tickLength: 0
                property real tickTurn: 0
                property real arcStart: 0
                // Up to a fifth of the circle, eased towards the value.
                property real arcSpan: 0.003 + 0.197 * share
                Behavior on arcSpan { NumberAnimation { duration: 700; easing.type: Easing.OutCubic } }
                property real pulseAngle: 0
                property real pulseStrength: 0
                property real glitch: core.ctl.instability
                property real intensity: 0.9
                property real dim: 0
                property real seed: 9
                property color color: modelData.v > 85 ? JarvisTheme.amber : Qt.lighter(core.ctl.colorA, 1.1)
                property color alertColor: core.ctl.alertColor
                property real alertMix: 0
                fragmentShader: "shaders/ring.frag.qsb"
            }
        }
        NeuralNetworkView {
            id: network
            anchors.fill: parent
            ctl: core.ctl
            graph: core.graph
        }
    }

    DataStreams {
        anchors.fill: parent
        ctl: core.ctl
        visible: !core.compact && intensity > 0.01
    }

    // Readouts: real numbers only (state, route, stream, counters).
    component Readout: Column {
        property alias label: cap.text
        property alias value: val.text
        property int align: Text.AlignLeft
        spacing: 1
        opacity: 0.55 + 0.45 * Math.min(1, core.ctl.energy + (core.hovered ? 0.4 : 0))
        Text { id: cap; color: JarvisTheme.textDim; font.family: JarvisTheme.mono; font.pixelSize: 10; font.letterSpacing: 2; horizontalAlignment: parent.align; width: parent.width }
        Text { id: val; color: core.ctl.colorA; font.family: JarvisTheme.mono; font.pixelSize: 13; font.bold: true; horizontalAlignment: parent.align; width: parent.width; elide: Text.ElideRight }
    }
    Item {
        visible: !core.compact && core.size > 300
        anchors.centerIn: parent
        width: core.size
        height: core.size
        Readout {
            x: 0; y: 0; width: parent.width * 0.26
            label: qsTr("STATE")
            value: JarvisTheme.stateName(core.vs.state).toUpperCase() + (core.vs.demo ? qsTr(" · DEMO") : "")
        }
        Readout {
            x: parent.width * 0.74; y: 0; width: parent.width * 0.26
            align: Text.AlignRight
            label: qsTr("ROUTE")
            value: core.vs.routes.length > 0 ? core.vs.routes.map(JarvisTheme.routeName).join(" › ").toUpperCase() : "—"
        }
        Readout {
            x: 0; y: parent.height - height; width: parent.width * 0.26
            label: qsTr("REQUESTS · ERRORS")
            value: core.vs.counters.requests + " · " + core.vs.counters.errors
        }
        Readout {
            x: parent.width * 0.74; y: parent.height - height; width: parent.width * 0.26
            align: Text.AlignRight
            label: core.vs.streaming ? qsTr("STREAM") : qsTr("LAST ANSWER")
            value: core.vs.streaming ? qsTr("%1 ch/s").arg(Math.round(core.vs.streamRate))
                                     : (core.vs.lastLatency > 0 ? qsTr("%1 s").arg((core.vs.lastLatency / 1000).toFixed(1)) : "—")
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: core.activated() }
}
