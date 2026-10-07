pragma Singleton

import QtQuick

// Shared translated labels and formatting for the memory and activity views.
QtObject {
    function categoryName(id) {
        switch (id) {
        case "coding": return qsTr("Coding")
        case "terminal": return qsTr("Terminal")
        case "browsing": return qsTr("Web")
        case "communication": return qsTr("Communication")
        case "office": return qsTr("Documents")
        case "design": return qsTr("Creative work")
        case "media": return qsTr("Media")
        case "gaming": return qsTr("Games")
        case "files": return qsTr("Files")
        case "system": return qsTr("System")
        default: return qsTr("Other")
        }
    }

    function slotLabel(slot) {
        switch (slot) {
        case "name": return qsTr("Name")
        case "location": return qsTr("Location")
        case "occupation": return qsTr("Occupation")
        case "project": return qsTr("Project")
        case "birthday": return qsTr("Birthday")
        case "likes": return qsTr("Likes")
        case "dislikes": return qsTr("Dislikes")
        case "skill": return qsTr("Codes in")
        case "uses": return qsTr("Uses")
        case "goal": return qsTr("Goal")
        case "active_hours": return qsTr("Active hours")
        default: return qsTr("Note")
        }
    }

    function sourceLabel(source) {
        switch (source) {
        case "dialog": return qsTr("from our chat")
        case "claude": return qsTr("noted by Claude")
        case "reflection": return qsTr("noticed in our chats")
        case "curiosity": return qsTr("you answered my question")
        case "activity": return qsTr("from your activity")
        default: return qsTr("you asked to remember")
        }
    }

    function duration(seconds) {
        const minutes = Math.round(seconds / 60)
        if (minutes < 1)
            return qsTr("<1 min")
        if (minutes < 60)
            return qsTr("%1 min").arg(minutes)
        const h = Math.floor(minutes / 60), m = minutes % 60
        return m === 0 ? qsTr("%1 h").arg(h) : qsTr("%1 h %2 min").arg(h).arg(m)
    }

    function trackingStatus(status) {
        if (status === "kwin")
            return qsTr("Watching the focused window through KWin.")
        if (status === "no-kwin")
            return qsTr("Waiting for KWin: tracking starts by itself as soon as the Plasma session is ready. Outside KDE Plasma it stays off.")
        if (status && status.indexOf("error:") === 0)
            return qsTr("Could not start tracking: %1").arg(status.substring(6))
        return ""
    }
}
