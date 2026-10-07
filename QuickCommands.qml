pragma Singleton

import QtQuick

// Quick actions shared by the quick bar and the activity panel. Every run is
// recorded so Jarvis can learn which actions the user needs at which hour.
QtObject {
    readonly property var base: ["ask:memory", "ask:cpu", "ask:disk", "launch:files", "launch:terminal", "launch:settings"]

    function label(id, fallback) {
        switch (id) {
        case "ask:memory": return qsTr("Memory")
        case "ask:cpu": return qsTr("CPU")
        case "ask:disk": return qsTr("Disk")
        case "ask:today": return qsTr("My day")
        case "launch:files": return qsTr("Files")
        case "launch:terminal": return qsTr("Terminal")
        case "launch:settings": return qsTr("KDE settings")
        case "web": return qsTr("Web search")
        default: return fallback || id
        }
    }

    // The local engine understands both languages; the wording decides the reply language.
    function question(id, lang) {
        const ru = lang === "ru"
        switch (id) {
        case "memory": return ru ? "память" : "memory"
        case "cpu": return ru ? "загрузка процессора" : "cpu usage"
        case "disk": return ru ? "диск" : "disk space"
        case "today": return ru ? "чем я занимался сегодня" : "what did I do today"
        default: return id
        }
    }

    // Returns "" or an error message for the user.
    function run(id, fallbackLabel, chat, desktop, lang) {
        if (id.indexOf("ask:") === 0 && chat.busy)
            return qsTr("Jarvis is still answering.")
        chat.recordAction(id, label(id, fallbackLabel))
        if (id.indexOf("ask:") === 0) {
            chat.send(question(id.substring(4), lang))
            return ""
        }
        if (id.indexOf("launch:") === 0)
            return desktop.launch(id.substring(7))
        if (id.indexOf("app:") === 0)
            return desktop.launch(id)
        return ""
    }
}
