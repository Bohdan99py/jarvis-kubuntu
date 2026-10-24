import QtQuick
import org.jarvis.visual as JV

// Jarvis itself: the service, the language model, the task at hand, the
// microphone and the voice, cognitive awareness, the workspace and the last
// events — all from jarvisd's status; nothing here is guessed.
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)

    readonly property var aw: status.awareness || {}
    readonly property var voice: status.voice || {}
    readonly property var ws: status.workspace || {}
    readonly property var activity: status.activity || {}
    readonly property bool working: status.state === "processing"
    function routeName(r) { return JV.JarvisTheme.routeName(r) }
    function eventText(e) {
        switch (e.kind) {
        case "route": return i18nd("jarvis-desktop", "request → %1", routeName(e.text))
        case "answer": return i18nd("jarvis-desktop", "answered (%1)", e.text.split(" › ").map(routeName).join(" › "))
        case "error": return i18nd("jarvis-desktop", "Claude could not answer")
        case "notice": return i18nd("jarvis-desktop", "notification: %1", e.text)
        case "workspace": return i18nd("jarvis-desktop", "workspace %1", e.text)
        case "launch": return i18nd("jarvis-desktop", "opened %1", e.text)
        case "background": return i18nd("jarvis-desktop", "background: %1", routeName(e.text))
        default: return e.kind + (e.text ? ": " + e.text : "")
        }
    }
    function clock(ms) { return new Date(ms).toLocaleTimeString(Qt.locale(), "HH:mm") }

    component Line: Row {
        property string key
        property string value
        property color tint: JV.JarvisTheme.text
        width: view.width - 20
        spacing: 8
        Text { width: 118; text: parent.key; color: JV.JarvisTheme.textDim; font.family: JV.JarvisTheme.mono; font.pixelSize: 10; elide: Text.ElideRight }
        Text { width: parent.width - 126; text: parent.value; color: parent.tint; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
    }

    JV.HolographicPanel {
        width: parent.width
        panelWidth: parent.width
        title: "J.A.R.V.I.S."
        tag: view.online ? (view.status.version || "") : i18nd("jarvis-desktop", "OFFLINE")
        accent: JV.JarvisTheme.stateColor(view.online ? (view.status.state || "idle") : "offline")
        shown: true
        movable: false
        busy: view.effects && view.working
        Column {
            width: parent.width
            spacing: 3 * JV.JarvisTheme.spacing
            Line {
                key: i18nd("jarvis-desktop", "SERVICE")
                value: !view.online ? i18nd("jarvis-desktop", "not running")
                     : view.status.workspace === undefined ? i18nd("jarvis-desktop", "running an older version: restart it")
                     : JV.JarvisTheme.stateName(view.status.state || "idle") + (view.status.integration === false ? " · " + i18nd("jarvis-desktop", "integration off") : "")
                tint: view.online ? JV.JarvisTheme.text : JV.JarvisTheme.amber
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "MODEL")
                value: view.status.claude ? (view.status.model || "Claude") + (view.status.webSearch ? " · " + i18nd("jarvis-desktop", "web search") : "")
                                          : i18nd("jarvis-desktop", "offline answers (no Claude key)")
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "TASK")
                value: view.working ? i18nd("jarvis-desktop", "answering · %1", (view.activity.routes || []).map(view.routeName).join(" › "))
                     : view.status.task ? view.status.task.text + (view.status.task.due > 0 ? " · " + new Date(view.status.task.due).toLocaleString(Qt.locale(), "dd.MM HH:mm") : "")
                     : i18nd("jarvis-desktop", "nothing pending")
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "MIC · VOICE")
                value: view.voice.at === undefined ? i18nd("jarvis-desktop", "unknown (the Jarvis window is closed)")
                     : (view.voice.listening ? i18nd("jarvis-desktop", "listening") : i18nd("jarvis-desktop", "microphone off"))
                       + " · " + (view.voice.speaking ? i18nd("jarvis-desktop", "speaking") : i18nd("jarvis-desktop", "silent"))
                       + (view.voice.engine ? " (" + view.voice.engine + ")" : "")
                tint: view.voice.listening ? JV.JarvisTheme.amber : JV.JarvisTheme.text
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "AWARENESS")
                value: (view.aw.level || "—") + (view.aw.paused ? " · " + i18nd("jarvis-desktop", "paused") : "") + (view.aw.dnd ? " · " + i18nd("jarvis-desktop", "do not disturb") : "")
            }
            Line {
                visible: view.online && view.ws.current !== undefined
                key: i18nd("jarvis-desktop", "WORKSPACE")
                value: (view.ws.current || "").toUpperCase() + (view.ws.previous ? " · " + i18nd("jarvis-desktop", "before: %1", view.ws.previous.toUpperCase()) : "")
            }
            Column {
                visible: view.extra && view.online && (view.status.events || []).length > 0
                width: parent.width
                spacing: 1
                Text { text: i18nd("jarvis-desktop", "LAST EVENTS"); color: JV.JarvisTheme.textDim; font.family: JV.JarvisTheme.mono; font.pixelSize: 10; font.letterSpacing: 2 }
                Repeater {
                    model: (view.status.events || []).slice(-5).reverse()
                    Text {
                        required property var modelData
                        width: view.width - 20
                        text: view.clock(modelData.at) + "  " + view.eventText(modelData)
                        color: modelData.kind === "error" ? JV.JarvisTheme.red : JV.JarvisTheme.textDim
                        font.pixelSize: 10; elide: Text.ElideRight; textFormat: Text.PlainText
                    }
                }
            }
            Row {
                spacing: 4
                JV.HoloButton { visible: view.online; text: i18nd("jarvis-desktop", "Overlay"); glyph: "◎"; primary: true; enabled: view.status.integration === true; onClicked: view.action("overlay", "") }
                JV.HoloButton { text: view.online ? i18nd("jarvis-desktop", "Open Jarvis") : i18nd("jarvis-desktop", "Start Jarvis"); onClicked: view.action(view.online ? "main" : "start", "") }
            }
        }
    }
}
