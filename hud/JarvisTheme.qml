pragma Singleton

import QtQuick

// Colours and type of the cinematic HUD. The rest of the application keeps Theme.
QtObject {
    readonly property color void_: "#03060c"
    readonly property color glass: Qt.rgba(0.04, 0.09, 0.16, 0.72)
    readonly property color glassStrong: Qt.rgba(0.03, 0.07, 0.13, 0.9)
    readonly property color line: Qt.rgba(0.42, 0.85, 1.0, 0.28)
    readonly property color lineStrong: Qt.rgba(0.48, 0.9, 1.0, 0.7)
    readonly property color cyan: "#5fe3ff"
    readonly property color ice: "#bff4ff"
    readonly property color deep: "#0b3a66"
    readonly property color amber: "#ffb347"
    readonly property color red: "#ff4d5e"
    readonly property color text: "#d7f3ff"
    readonly property color textDim: "#7fa5bd"
    readonly property string mono: "DejaVu Sans Mono"

    // A state's name for people (the state itself stays a code).
    function stateName(state) {
        switch (state) {
        case "idle": return qsTr("idle")
        case "listening": return qsTr("listening")
        case "processing": return qsTr("processing")
        case "speaking": return qsTr("speaking")
        case "notification": return qsTr("notification")
        case "warning": return qsTr("warning")
        case "error": return qsTr("error")
        default: return state
        }
    }
    // What answered a request (VisualState routes).
    function routeName(r) {
        switch (r) {
        case "llm": return qsTr("Claude")
        case "recall": return qsTr("memory recall")
        case "learn": return qsTr("learning")
        case "memory": return qsTr("memory command")
        case "calculator": return qsTr("calculator")
        case "schematic": return qsTr("schematics library")
        case "system": return qsTr("live system data")
        case "tasks": return qsTr("reminders")
        case "files": return qsTr("attachments")
        case "code": return qsTr("code")
        case "local": return qsTr("local answer")
        case "reflect": return qsTr("memory review")
        default: return r
        }
    }

    // The core's colour in each state.
    function stateColor(state) {
        switch (state) {
        case "listening": return "#6ff0ff"
        case "processing": return "#6aa8ff"
        case "speaking": return "#8ff7ff"
        case "notification": return "#9ad6ff"
        case "warning": return "#ffc46b"
        case "error": return "#ff5a6a"
        default: return "#4fd8ff"
        }
    }
    function stateDeep(state) {
        switch (state) {
        case "processing": return "#122a6e"
        case "warning": return "#5a3200"
        case "error": return "#5a0812"
        default: return "#063457"
        }
    }
}
