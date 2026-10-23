.pragma library

// The palette of Jarvis's HUD (hud/JarvisTheme.qml) for the Plasma widget,
// which runs inside plasmashell and cannot import the application's QML
// module. tests/desktop_files_test.py keeps the two in step.
var void_ = "#03060c"
var glass = "#b80a1729"       // rgba(0.04, 0.09, 0.16, 0.72)
var glassStrong = "#e6081221" // rgba(0.03, 0.07, 0.13, 0.9)
var line = "#476bd9ff"        // rgba(0.42, 0.85, 1.0, 0.28)
var lineStrong = "#b37ae6ff"  // rgba(0.48, 0.9, 1.0, 0.7)
var cyan = "#5fe3ff"
var ice = "#bff4ff"
var deep = "#0b3a66"
var amber = "#ffb347"
var red = "#ff4d5e"
var text = "#d7f3ff"
var textDim = "#7fa5bd"
var mono = "DejaVu Sans Mono"
// Not in the application's theme: the core while the service is not running.
var offline = "#5b6b78"

// The core's colour in each state.
function stateColor(state) {
    switch (state) {
    case "listening": return "#6ff0ff"
    case "processing": return "#6aa8ff"
    case "speaking": return "#8ff7ff"
    case "notification": return "#9ad6ff"
    case "warning": return "#ffc46b"
    case "error": return "#ff5a6a"
    case "offline": return offline
    default: return "#4fd8ff"
    }
}
function stateDeep(state) {
    switch (state) {
    case "processing": return "#122a6e"
    case "warning": return "#5a3200"
    case "error": return "#5a0812"
    case "offline": return "#1a2129"
    default: return "#063457"
    }
}
