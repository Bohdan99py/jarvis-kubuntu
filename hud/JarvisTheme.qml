pragma Singleton

import QtQuick

// Colours and type of the cinematic HUD. The rest of the application keeps Theme.
//
// One set of tokens for the window's HUD and the Plasma desktop (0.19.2: the
// same file is installed as the QML module org.jarvis.visual). Three presets —
// classic (the 0.17/0.18 look, unchanged), mark7 (amber and red, more rings)
// and minimal (calm, few effects) — and the user's own primary and secondary
// colours on top of any of them.
QtObject {
    property string preset: "classic"          // classic, mark7, minimal
    property color customPrimary: "transparent" // alpha 0: the preset's colour
    property color customSecondary: "transparent"
    property real glow: 1.0                     // 0..1.5, the brightness of lines and halos
    property real glassOpacity: 0.72            // panels' glass
    property real motion: 1.0                   // animation speed, 0.5..1.5
    property string density: "normal"          // compact, normal, comfortable
    readonly property bool mark7: preset === "mark7"
    readonly property bool minimal: preset === "minimal"
    readonly property bool classicColours: preset === "classic" && customPrimary.a === 0 && customSecondary.a === 0
    readonly property real spacing: density === "compact" ? 0.8 : density === "comfortable" ? 1.25 : 1

    readonly property color primary: customPrimary.a > 0 ? customPrimary : mark7 ? "#ffb347" : minimal ? "#8fd6ea" : "#5fe3ff"
    readonly property color secondary: customSecondary.a > 0 ? customSecondary : mark7 ? "#ff5a3c" : minimal ? "#a9bfcc" : "#ffb347"

    readonly property color void_: "#03060c"
    readonly property color glass: Qt.rgba(0.04, 0.09, 0.16, glassOpacity)
    readonly property color glassStrong: Qt.rgba(0.03, 0.07, 0.13, Math.min(1, glassOpacity + 0.18))
    readonly property color line: classicColours && glow === 1 ? Qt.rgba(0.42, 0.85, 1.0, 0.28) : Qt.rgba(primary.r, primary.g, primary.b, Math.min(1, 0.28 * glow))
    readonly property color lineStrong: classicColours && glow === 1 ? Qt.rgba(0.48, 0.9, 1.0, 0.7) : Qt.rgba(primary.r, primary.g, primary.b, Math.min(1, 0.7 * glow))
    readonly property color cyan: primary
    readonly property color ice: classicColours ? "#bff4ff" : Qt.lighter(primary, 1.5)
    readonly property color deep: classicColours ? "#0b3a66" : Qt.darker(primary, 3.2)
    readonly property color amber: secondary
    readonly property color red: "#ff4d5e"
    readonly property color text: "#d7f3ff"
    readonly property color textDim: "#7fa5bd"
    readonly property string mono: "DejaVu Sans Mono"

    // 0.19.3: one set of surface tokens for the dock, the launcher, the desktop
    // widgets and the overlay. Surfaces that carry text never drop below a
    // readable opacity, whatever the glass setting says: on a light wallpaper
    // the panels stay dark enough for the text (contrast ≥ 4.5 : 1), and
    // `lightBackdrop` (jarvisd measures the wallpaper) adds a firmer edge.
    property bool lightBackdrop: false
    // The desktop's quality (low, balanced, cinematic): decorative motion of live numbers only in cinematic.
    property string quality: "balanced"
    readonly property real surfaceAlpha: Math.max(lightBackdrop ? 0.88 : 0.8, glassOpacity)
    readonly property color surface: minimal ? Qt.rgba(0.06, 0.08, 0.11, surfaceAlpha)
                                     : mark7 ? Qt.rgba(0.09, 0.05, 0.04, surfaceAlpha) : Qt.rgba(0.03, 0.08, 0.14, surfaceAlpha)
    readonly property color surfaceRaised: Qt.rgba(primary.r, primary.g, primary.b, minimal ? 0.06 : 0.09)
    readonly property color hoverFill: Qt.rgba(primary.r, primary.g, primary.b, 0.16)
    readonly property color selectFill: Qt.rgba(primary.r, primary.g, primary.b, 0.26)
    readonly property color edge: Qt.rgba(primary.r, primary.g, primary.b, Math.min(1, (lightBackdrop ? 0.55 : 0.32) * Math.max(0.4, glow)))
    readonly property color edgeStrong: Qt.rgba(primary.r, primary.g, primary.b, Math.min(1, 0.8 * Math.max(0.5, glow)))
    // A dark outline under the glass: what keeps a panel visible on a white wallpaper.
    readonly property color outline: Qt.rgba(0, 0, 0, lightBackdrop ? 0.55 : 0.3)
    readonly property color textSoft: "#a9c8da"         // secondary text on `surface` (6 : 1)
    readonly property color positive: "#6fe0a8"
    // Type scale: nothing a person has to read is smaller than `fontSmall`.
    readonly property int fontSmall: Math.round(11 * spacing)
    readonly property int fontBody: Math.round(13 * spacing)
    readonly property int fontTitle: Math.round(12 * spacing)
    readonly property int fontLarge: Math.round(18 * spacing)
    readonly property int radius: minimal ? 10 : 12
    readonly property int radiusSmall: 7
    // A colour with another alpha (QML has no shorthand).
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }

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

    // The core's colour in each state (classic: the original palette; otherwise
    // derived from the primary and secondary colours).
    function stateColor(state) {
        if (!classicColours) {
            switch (state) {
            case "listening": return Qt.lighter(primary, 1.18)
            case "processing": return Qt.tint(primary, "#806aa8ff")
            case "speaking": return Qt.lighter(primary, 1.3)
            case "notification": return Qt.lighter(primary, 1.1)
            case "warning": return secondary
            case "error": return "#ff5a6a"
            case "offline": return "#5b6b78"
            default: return primary
            }
        }
        switch (state) {
        case "listening": return "#6ff0ff"
        case "processing": return "#6aa8ff"
        case "speaking": return "#8ff7ff"
        case "notification": return "#9ad6ff"
        case "warning": return "#ffc46b"
        case "error": return "#ff5a6a"
        case "offline": return "#5b6b78"
        default: return "#4fd8ff"
        }
    }
    function stateDeep(state) {
        switch (state) {
        case "processing": return "#122a6e"
        case "warning": return "#5a3200"
        case "error": return "#5a0812"
        case "offline": return "#1a2129"
        default: return classicColours ? "#063457" : Qt.darker(primary, 4)
        }
    }
}
