import QtQuick

// The analyser ring around the core. It is driven only by measured sound:
// the voice's three bands while Jarvis speaks (speak.py meters what aplay plays)
// and the microphone level while it listens. Without sound it is almost dark.
ShaderEffect {
    id: layer
    required property var ctl

    property real time: ctl.time
    property real pixel: 2 / Math.max(1, width)
    property real innerRadius: 0.30 + 0.03 * ctl.openness
    property real maxLength: 0.14 + 0.05 * ctl.openness
    property real bars: ctl.low ? 48 : 96
    property real turn: ctl.time * 0.01
    property vector4d bands: Qt.vector4d(ctl.sLow, ctl.sMid, ctl.sHigh, ctl.sMic)
    property vector4d historyA: Qt.vector4d(ctl.history[0], ctl.history[1], ctl.history[2], ctl.history[3])
    property vector4d historyB: Qt.vector4d(ctl.history[4], ctl.history[5], ctl.history[6], ctl.history[7])
    property color color: Qt.lighter(ctl.colorA, 1.15)
    property real intensity: 0.25 + 0.9 * Math.min(1, ctl.sLevel + ctl.sMic)

    visible: ctl.sLevel + ctl.sMic + ctl.sLow > 0.004
    fragmentShader: "shaders/spectrum.frag.qsb"
}
