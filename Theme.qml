pragma Singleton

import QtQuick

QtObject {
    readonly property color bg: "#080e19"
    readonly property color panel: "#111c2c"
    readonly property color border: "#233449"
    readonly property color text: "#e6edf3"
    readonly property color textDim: "#8b98a8"
    readonly property color accent: "#50d9d4"
    readonly property color userBubble: "#315c8e"
    readonly property color botBubble: "#16263a"
    readonly property color danger: "#da3633"
    readonly property color hover: "#2a3544"
    readonly property color selected: "#203b4c"
    // Memory graph node kinds.
    readonly property color nodeExample: "#50d9d4"
    readonly property color nodeFact: "#f0b45a"
    readonly property color nodeTopic: "#8f9cff"
    readonly property int fontSize: 15

    function categoryColor(id) {
        switch (id) {
        case "coding": return "#50d9d4"
        case "terminal": return "#3fb27f"
        case "browsing": return "#5aa2f0"
        case "communication": return "#c58af9"
        case "office": return "#f0b45a"
        case "design": return "#f07a8f"
        case "media": return "#e46fb8"
        case "gaming": return "#9ad04f"
        case "files": return "#c9a66b"
        case "system": return "#8b98a8"
        default: return "#6f7f92"
        }
    }
}
