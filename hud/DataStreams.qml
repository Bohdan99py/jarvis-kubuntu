import QtQuick

// Data on its way: while a request is processed, beams converge on the core;
// when the answer streams in, packets fly out of it at a speed that follows the
// real rate of the answer (characters per second). Invisible otherwise, so it
// costs nothing at rest.
ShaderEffect {
    id: streams
    required property var ctl

    property real time: ctl.time
    property real pixel: 2 / Math.max(1, height)
    property real aspect: width / Math.max(1, height)
    property real beams: ctl.low ? 7 : 13
    property real turn: 0
    property real travel: 0
    property real direction: ctl.streaming ? 1 : -1
    property real innerRadius: 0.2
    property real intensity: ctl.streamIntensity
    property color color: Qt.lighter(ctl.colorA, 1.2)

    visible: intensity > 0.01
    fragmentShader: "shaders/streams.frag.qsb"

    Connections {
        target: streams.ctl
        enabled: streams.visible
        function onTick(dt) {
            streams.travel += dt * (0.5 + Math.min(3, streams.ctl.streamRate / 300))
            streams.turn = (streams.turn + dt * 0.006) % 1
        }
    }
}
