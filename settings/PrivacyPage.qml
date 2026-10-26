import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// Settings → Memory and privacy: what is kept and sent, memory, activity, the
// sources of context (window, screen), excluded applications, pause, and
// deleting saved data (each kind with its own confirmation).
ColumnLayout {
    id: page
    required property var win
    readonly property var settings: win.settings
    readonly property var chat: win.chat
    readonly property var ctx: { try { return JSON.parse(chat.context) } catch (e) { return {} } }
    readonly property bool paused: (ctx.pausedUntil || 0) > 0
    readonly property bool observing: settings.awarenessLevel !== "off"

    spacing: 6

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        SettingGroup { title: qsTr("Overview") }
        SettingRow {
            entry: "data.storage"
            title: qsTr("What is stored and what is sent")
            hint: qsTr("On this computer only (~/.local/share/jarvis, owner-only files): facts and topics Jarvis learned, conversation summaries, tasks and reminders, activity history (14 days), study progress. Screenshots and window titles are never written to disk.")
                  + "\n" + qsTr("Sent to Anthropic, only with a question that needs Claude: the question, the facts relevant to it, and — if you allow it below — your current activity, the recognised text of the screen or the screenshot. Sent to ElevenLabs, only with that voice on: the text being spoken.")
                  + "\n" + (!page.settings.keyringAvailable ? qsTr("API keys: in a settings file only you can read (there is no system password store here).")
                                                                : qsTr("API keys: in the system password store."))
        }

        // ---- Memory ----
        SettingGroup { title: qsTr("Memory") }
        SettingSwitch {
            objectName: "learnDialogSwitch"
            entry: "memory.learn"
            title: qsTr("Learn from our conversations")
            hint: qsTr("Facts like your name, tools and projects, topics you talk about, corrections and the quick actions you use. With a Claude key, Jarvis also keeps a short summary of a finished conversation. Passwords, keys and card numbers are never stored.")
            checked: page.settings.learnDialog
            commit: on => page.settings.setOption("learnDialog", on)
        }
        SettingSwitch {
            objectName: "curiositySwitch"
            entry: "memory.curiosity"
            enabled: page.settings.learnDialog
            title: qsTr("Jarvis asks questions to get to know me")
            hint: qsTr("At most every ten minutes and never in the middle of a task. Ignore a question to skip it.")
            checked: page.settings.curiosity
            commit: on => page.settings.setOption("curiosity", on)
        }
        SettingSwitch {
            entry: "memory.reflection"
            enabled: page.settings.learnDialog && page.settings.hasKey
            title: qsTr("Review my memory every day")
            hint: qsTr("Once a day Jarvis sends the facts it keeps about you and short summaries of your conversations to Claude to merge duplicates and drop outdated facts. Facts you added yourself are never deleted.")
            checked: page.settings.deepReflection
            commit: on => page.settings.setOption("deepReflection", on)
        }
        SettingRow {
            entry: "memory.panel"
            title: qsTr("Review what Jarvis remembers")
            hint: qsTr("The memory panel: facts, topics, tasks and conversations; each entry can be deleted there. In chat: \"what do you know about me\", \"forget …\".")
            DarkButton { text: qsTr("Open the memory panel"); onClicked: page.win.memoryRequested() }
        }

        // ---- Activity ----
        SettingGroup { title: qsTr("Activity") }
        SettingSwitch {
            objectName: "trackActivitySwitch"
            entry: "activity.track"
            title: qsTr("See which application I'm working in")
            hint: qsTr("A small KWin script reports the focused window. Time per application, habits and suggestions are kept for 14 days. Nothing is recorded while the screen is locked.")
            checked: page.settings.trackActivity
            commit: on => page.settings.setOption("trackActivity", on)
        }
        SettingSwitch {
            entry: "activity.titles"
            enabled: page.settings.trackActivity
            title: qsTr("Also keep window titles")
            hint: qsTr("Titles show the document or page (for example \"main.cpp — jarvis\"). Private browsing and password manager windows are always skipped.")
            checked: page.settings.trackTitles
            commit: on => page.settings.setOption("trackTitles", on)
        }
        SettingSwitch {
            entry: "activity.share"
            enabled: page.settings.trackActivity
            title: qsTr("Tell Claude what I'm doing")
            hint: qsTr("Adds the focused application (and title, if kept) and today's top apps to requests sent to Anthropic.")
            checked: page.settings.shareActivity
            commit: on => page.settings.setOption("shareActivity", on)
        }

        // ---- Context sources ----
        SettingGroup {
            title: qsTr("Context sources")
            text: page.observing ? qsTr("What Jarvis may look at to answer with context (Ctrl+J). Turning a source off stops it at once and forgets what was seen through it.")
                                 : qsTr("Initiative is Off (Actions and permissions): Jarvis observes nothing, whatever is allowed here.")
        }
        SettingSwitch {
            entry: "awareness.window"
            enabled: page.observing
            title: qsTr("Allow Jarvis to see the active window")
            hint: qsTr("The application and the title of the window you work in, through a small KWin script. Titles stay in memory only.")
            checked: page.settings.awarenessWindow
            commit: on => page.settings.setOption("awarenessWindow", on)
        }
        SettingSwitch {
            id: screenSwitch
            entry: "awareness.screen"
            enabled: page.observing
            title: qsTr("Allow analysis of the screen content")
            hint: qsTr("A screenshot of the window you were in, read by Tesseract on this computer, only when you ask (or, if set below, now and then). \"◉ SCREEN\" is shown whenever it happens. Screenshots are never saved.")
            checked: page.settings.awarenessScreen
            // Turning it on asks first; turning it off is immediate.
            commit: on => { if (on) { screenConsent.open(); return "" } return page.settings.setOption("awarenessScreen", false) }
        }
        SettingSwitch {
            entry: "awareness.cloud"
            enabled: page.settings.awarenessScreen && page.observing
            title: qsTr("Send screenshots to Claude")
            hint: qsTr("A question about the screen also carries the picture to Anthropic (scaled down) — better for drawings and schematics. Off: only the recognised text goes.")
            checked: page.settings.awarenessCloudImages
            commit: on => page.settings.setOption("awarenessCloudImages", on)
        }
        SettingCombo {
            entry: "awareness.interval"
            enabled: page.settings.awarenessScreen && (page.settings.awarenessLevel === "assistive" || page.settings.awarenessLevel === "proactive")
            title: qsTr("Background screen analysis")
            hint: qsTr("Only with offers of help on (Actions and permissions).")
            values: [0, 2, 5, 10, 15]
            labels: values.map(v => v === 0 ? qsTr("never (only when I ask)") : qsTr("at most every %1 min").arg(v))
            value: page.settings.screenIntervalMinutes
            commit: v => page.settings.setNumber("screenIntervalMinutes", v)
        }
        SettingRow {
            visible: !!page.win.screenAware
            title: qsTr("Text recognition")
            hint: page.win.screenAware ? (page.win.screenAware.ocrAvailable ? qsTr("Tesseract (%1).").arg(page.win.screenAware.ocrLanguages)
                                                                          : qsTr("Needs Tesseract: sudo apt install tesseract-ocr tesseract-ocr-fra tesseract-ocr-rus"))
                                         + (page.win.screenAware.status.length > 0 ? " " + page.win.screenAware.status : "") : ""
            DarkButton {
                text: qsTr("Test: read my screen now")
                enabled: !!page.win.screenAware && page.settings.awarenessScreen && page.observing && !page.win.screenAware.capturing
                onClicked: page.win.screenAware.test()
            }
        }
        SettingRow {
            id: excludedRow
            entry: "awareness.excluded"
            title: qsTr("Never observe these applications")
            hint: qsTr("Desktop file names or window classes, separated by commas (for example: org.telegram.desktop, firefox). Password managers, keyrings and authentication prompts are always excluded.")
            ColumnLayout {
                spacing: 6
                TextField {
                    id: excludedField
                    Layout.preferredWidth: 280
                    text: page.settings.awarenessExcluded.join(", ")
                    placeholderText: "org.telegram.desktop, thunderbird"
                    selectByMouse: true
                    Accessible.name: excludedRow.title
                }
                Row {
                    spacing: 6
                    DarkButton { text: qsTr("Save"); onClicked: excludedRow.error = page.settings.setList("awarenessExcluded", excludedField.text.split(",")) }
                    DarkButton {
                        text: qsTr("Exclude current")
                        enabled: !!page.ctx.appKey
                        onClicked: excludedRow.error = page.settings.setList("awarenessExcluded", page.settings.awarenessExcluded.concat([page.ctx.appKey]))
                    }
                }
            }
        }
        SettingRow {
            entry: "awareness.pause"
            title: qsTr("Pause observation")
            hint: !page.win.daemonUp ? qsTr("Needs the Jarvis service: it is not running.")
                  : page.paused ? qsTr("Paused until %1: no offers, no background screenshots.").arg(new Date(page.ctx.pausedUntil).toLocaleString(Qt.locale(), Locale.ShortFormat))
                  : qsTr("Observation is running at the chosen level.")
            Flow {
                width: SettingsState.narrow ? page.width - 60 : 330
                spacing: 6
                DarkButton { text: qsTr("1 hour"); enabled: page.win.daemonUp; onClicked: page.chat.pauseAwareness(60) }
                DarkButton { text: qsTr("Until tomorrow"); enabled: page.win.daemonUp; onClicked: page.chat.pauseAwareness(-1) }
                DarkButton { text: qsTr("Resume"); enabled: page.win.daemonUp && page.paused; onClicked: page.chat.pauseAwareness(0) }
            }
        }

        // ---- Saved data ----
        SettingGroup {
            title: qsTr("Saved data")
            text: qsTr("Deleting cannot be undone. Settings and API keys are not touched.")
        }
        Item { objectName: "entry:data.delete"; Layout.fillWidth: true; implicitHeight: 1 }
        Repeater {
            model: [["activity", qsTr("Activity history"), qsTr("Which applications you used and when (up to 14 days).")],
                    ["facts", qsTr("Facts, topics and conversation summaries"), qsTr("Everything Jarvis learned about you. Taught examples, flashcards and activity stay.")],
                    ["code", qsTr("Code lessons and answers"), qsTr("What Jarvis learned from your programming questions.")],
                    ["driving", qsTr("Driving theory progress"), qsTr("Answered questions and topics of the driving test.")],
                    ["awareness", qsTr("Observed context and offer history"), qsTr("What Jarvis saw on the screen and in windows, and the offers of the last 30 days.")]]
            SettingRow {
                required property var modelData
                title: modelData[1]
                hint: modelData[2]
                DarkButton { text: qsTr("Delete…"); danger: true; Accessible.name: qsTr("Delete %1").arg(modelData[1]); onClicked: { deleteConsent.what = modelData[0]; deleteConsent.label = modelData[1]; deleteConsent.open() } }
            }
        }
        Label { text: page.chat.memoryStatus; visible: text.length > 0; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 10; textFormat: Text.PlainText }
    }

    ConfirmDialog {
        id: screenConsent
        width: Math.min(480, page.win.width - 40)
        title: qsTr("Let Jarvis read your screen?")
        acceptText: qsTr("Allow")
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Jarvis will take a screenshot of the window you work in when you ask (and, if you set an interval, now and then in the background) and recognise its text on this computer. \"◉ SCREEN\" is shown every time. Screenshots are not saved, password and private windows are skipped, and nothing goes to Claude unless you ask a question. You can turn this off or pause it at any moment.")
        }
        onAccepted: screenSwitch.error = page.settings.setOption("awarenessScreen", true)
    }
    // Every kind of data has its own confirmation.
    ConfirmDialog {
        id: deleteConsent
        property string what
        property string label
        width: Math.min(460, page.win.width - 40)
        title: qsTr("Delete \"%1\"?").arg(label)
        acceptText: qsTr("Delete")
        destructive: true
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("This cannot be undone. Settings, API keys and other kinds of data stay.") }
        onAccepted: page.chat.forget(what)
    }
}
