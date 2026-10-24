import QtQuick
import QtQuick.Layouts
import org.jarvis.visual as JV

// Jarvis itself, said for a person (0.19.3): online or not, what it is doing,
// the language model by its name, the microphone and the voice, the task at
// hand, awareness and the workspace — from jarvisd's status; nothing here is
// guessed, and no internal code is shown where a word will do. The recent
// events are behind the title (click it).
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)
    implicitHeight: panel.height

    readonly property var aw: status.awareness || {}
    readonly property var voice: status.voice || {}
    readonly property var ws: status.workspace || {}
    readonly property var activity: status.activity || {}
    readonly property string state: online ? (status.state || "idle") : "offline"
    readonly property bool working: state === "processing"
    function routeName(r) { return JV.JarvisTheme.routeName(r) }
    // "claude-haiku-4-5-20251001" → "Claude Haiku 4.5"; anything else as it is.
    function modelName(id) {
        const m = /^claude-([a-z]+)-(\d+)(?:-(\d+))?(?:-\d{8})?$/.exec(String(id || ""))
        if (!m) return id || "Claude"
        return "Claude " + m[1].charAt(0).toUpperCase() + m[1].slice(1) + " " + m[2] + (m[3] ? "." + m[3] : "")
    }
    function doing() {
        switch (state) {
        case "processing": return i18nd("jarvis-desktop", "Working on a request") + ((activity.routes || []).length ? " · " + activity.routes.map(routeName).join(" › ") : "")
        case "listening": return i18nd("jarvis-desktop", "Listening to you")
        case "speaking": return i18nd("jarvis-desktop", "Speaking")
        case "notification": return status.notice ? i18nd("jarvis-desktop", "Showing: %1", status.notice.title) : i18nd("jarvis-desktop", "Showing a notification")
        case "warning": return i18nd("jarvis-desktop", "Needs your attention")
        case "error": return i18nd("jarvis-desktop", "The last request failed")
        case "offline": return i18nd("jarvis-desktop", "Not running")
        default: return i18nd("jarvis-desktop", "Ready, waiting for you")
        }
    }
    function awarenessText() {
        if (aw.paused) return i18nd("jarvis-desktop", "paused")
        switch (aw.level) {
        case "off": return i18nd("jarvis-desktop", "off — sees nothing")
        case "manual": return i18nd("jarvis-desktop", "only when you ask")
        case "assistive": return i18nd("jarvis-desktop", "notices context, suggests rarely")
        case "proactive": return i18nd("jarvis-desktop", "notices context, suggests")
        default: return aw.level || "—"
        }
    }
    function eventText(e) {
        switch (e.kind) {
        case "route": return i18nd("jarvis-desktop", "request → %1", routeName(e.text))
        case "answer": return i18nd("jarvis-desktop", "answered (%1)", String(e.text || "").split(" › ").map(routeName).join(" › "))
        case "error": return i18nd("jarvis-desktop", "Claude could not answer")
        case "notice": return i18nd("jarvis-desktop", "notification: %1", e.text || "")
        case "workspace": return i18nd("jarvis-desktop", "workspace %1", e.text || "")
        case "launch": return i18nd("jarvis-desktop", "opened %1", e.text || "")
        case "background": return i18nd("jarvis-desktop", "background: %1", routeName(e.text))
        case "power": return i18nd("jarvis-desktop", "power profile: %1", e.text || "")
        default: return e.kind + (e.text ? ": " + e.text : "")
        }
    }
    function clock(ms) { return new Date(ms).toLocaleTimeString(Qt.locale(), "HH:mm") }

    component Line: RowLayout {
        property string key
        property string value
        property color tint: JV.JarvisTheme.text
        width: parent ? parent.width : 0
        spacing: 10
        Text {
            Layout.preferredWidth: Math.min(120, view.width * 0.32)
            text: parent.key; color: JV.JarvisTheme.textSoft
            font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: parent.value; color: parent.tint
            font.pixelSize: JV.JarvisTheme.fontSmall + 1
            elide: Text.ElideRight; textFormat: Text.PlainText
        }
    }

    JV.HolographicPanel {
        id: panel
        width: parent.width
        panelWidth: parent.width
        title: "J.A.R.V.I.S."
        tag: view.online ? (view.status.version || "") : i18nd("jarvis-desktop", "OFFLINE")
        accent: JV.JarvisTheme.stateColor(view.state)
        shown: true
        movable: false
        busy: view.effects && view.working
        Column {
            width: parent.width
            spacing: Math.round(5 * JV.JarvisTheme.spacing)
            // The headline: a state light and what Jarvis is doing, in words.
            RowLayout {
                width: parent.width
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 12; Layout.preferredHeight: 12
                    radius: 6
                    color: JV.JarvisTheme.stateColor(view.state)
                    border.color: Qt.rgba(1, 1, 1, 0.35)
                    SequentialAnimation on opacity {
                        running: view.effects && (view.working || view.state === "listening")
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.35; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                    }
                }
                Column {
                    Layout.fillWidth: true
                    Text {
                        width: parent.width
                        text: view.online ? i18nd("jarvis-desktop", "Online") : i18nd("jarvis-desktop", "Offline")
                        color: view.online ? JV.JarvisTheme.text : JV.JarvisTheme.amber
                        font.pixelSize: JV.JarvisTheme.fontBody + 2; font.bold: true
                    }
                    Text {
                        width: parent.width
                        text: view.status.workspace === undefined && view.online ? i18nd("jarvis-desktop", "An older Jarvis service is running: restart it") : view.doing()
                        color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall + 1
                        elide: Text.ElideRight; textFormat: Text.PlainText
                    }
                }
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "MODEL")
                value: view.status.claude ? view.modelName(view.status.model) + (view.status.webSearch ? " · " + i18nd("jarvis-desktop", "web search") : "")
                                          : i18nd("jarvis-desktop", "offline answers (no Claude key)")
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "VOICE")
                value: view.voice.at === undefined ? i18nd("jarvis-desktop", "unknown (the Jarvis window is closed)")
                     : (view.voice.listening ? i18nd("jarvis-desktop", "microphone on") : i18nd("jarvis-desktop", "microphone off"))
                       + " · " + (view.voice.speaking ? i18nd("jarvis-desktop", "speaking") : i18nd("jarvis-desktop", "silent"))
                tint: view.voice.listening ? JV.JarvisTheme.amber : JV.JarvisTheme.text
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "TASK")
                value: view.status.task ? view.status.task.text + (view.status.task.due > 0 ? " · " + new Date(view.status.task.due).toLocaleString(Qt.locale(), "dd.MM HH:mm") : "")
                     : i18nd("jarvis-desktop", "nothing pending")
            }
            Line {
                visible: view.online
                key: i18nd("jarvis-desktop", "AWARENESS")
                value: view.awarenessText() + (view.aw.dnd ? " · " + i18nd("jarvis-desktop", "do not disturb") : "")
            }
            Line {
                visible: view.online && view.ws.current !== undefined
                key: i18nd("jarvis-desktop", "WORKSPACE")
                value: (view.ws.current || "").toUpperCase() + (view.ws.previous ? " · " + i18nd("jarvis-desktop", "before: %1", view.ws.previous.toUpperCase()) : "")
            }
            Row {
                spacing: 6
                JV.HoloButton { visible: view.online; text: i18nd("jarvis-desktop", "Overlay"); glyph: "◎"; primary: true; enabled: view.status.integration === true; onClicked: view.action("overlay", "") }
                JV.HoloButton { text: view.online ? i18nd("jarvis-desktop", "Open Jarvis") : i18nd("jarvis-desktop", "Start Jarvis"); onClicked: view.action(view.online ? "main" : "start", "") }
            }
        }
        details: Column {
            width: parent ? parent.width : 0
            spacing: 2
            visible: view.extra && view.online
            Text {
                text: i18nd("jarvis-desktop", "LAST EVENTS") + (view.activity.requests !== undefined ? "  ·  " + i18nd("jarvis-desktop", "%1 requests, %2 failed", view.activity.requests, view.activity.errors || 0) : "")
                color: JV.JarvisTheme.textSoft; font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall; font.letterSpacing: 1
                width: parent.width; elide: Text.ElideRight
            }
            Repeater {
                model: (view.status.events || []).slice(-5).reverse()
                Text {
                    required property var modelData
                    width: parent.width
                    text: view.clock(modelData.at) + "  " + view.eventText(modelData)
                    color: modelData.kind === "error" ? JV.JarvisTheme.red : JV.JarvisTheme.textSoft
                    font.pixelSize: JV.JarvisTheme.fontSmall; elide: Text.ElideRight; textFormat: Text.PlainText
                }
            }
            Text {
                visible: (view.status.events || []).length === 0
                text: i18nd("jarvis-desktop", "Nothing has happened yet.")
                color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall
            }
        }
    }
}
