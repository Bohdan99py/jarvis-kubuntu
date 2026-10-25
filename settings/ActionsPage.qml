import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// Settings → Actions and permissions: how much initiative Jarvis takes, offers
// of help, hints, what desktop components may do, and what Jarvis always asks.
ColumnLayout {
    id: page
    required property var win
    readonly property var settings: win.settings
    readonly property var ctx: { try { return JSON.parse(win.chat.context) } catch (e) { return {} } }
    readonly property var stats: ctx.suggestions || {}
    readonly property string level: settings.awarenessLevel
    readonly property bool offers: level === "assistive" || level === "proactive"

    spacing: 6

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        // ---- Initiative ----
        SettingGroup {
            title: qsTr("Initiative")
            text: qsTr("One useful offer beats ten notifications: Jarvis offers help only on evidence — a repeated error, a recognised test question — never because time passed. Never at night, in Do Not Disturb, while the screen is locked or in games and films.")
        }
        SettingRow {
            id: levelRow
            entry: "awareness.level"
            title: qsTr("How much initiative Jarvis takes")
            ColumnLayout {
                spacing: 0
                Repeater {
                    model: [["off", qsTr("Off"), qsTr("No observation at all.")],
                            ["manual", qsTr("Only when I ask"), qsTr("Context for your questions (Ctrl+J). No offers.")],
                            ["assistive", qsTr("Rare offers"), qsTr("Also offers backed by strong evidence.")],
                            ["proactive", qsTr("More offers"), qsTr("Also electronics, documentation and studying.")]]
                    RadioButton {
                        id: levelButton
                        required property var modelData
                        Layout.preferredWidth: SettingsState.narrow ? page.width - 60 : 330
                        checked: page.level === modelData[0]
                        text: modelData[1]
                        Accessible.name: modelData[1] + ". " + modelData[2]
                        onClicked: {
                            levelRow.error = page.settings.setAwarenessLevel(modelData[0])
                            checked = Qt.binding(() => page.level === levelButton.modelData[0])
                        }
                        contentItem: ColumnLayout {
                            spacing: 0
                            Label { leftPadding: levelButton.indicator.width + 8; text: levelButton.modelData[1]; color: Theme.text; font.pixelSize: 14; font.bold: levelButton.checked }
                            Label { leftPadding: levelButton.indicator.width + 8; text: levelButton.modelData[2]; color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }

        // ---- Offers of help ----
        SettingGroup { title: qsTr("Offers of help"); text: page.offers ? "" : qsTr("Choose \"Rare offers\" or \"More offers\" above to use these.") }
        SettingCombo {
            entry: "offers.limits"
            enabled: page.offers
            title: qsTr("At most one offer every")
            values: [20, 45, 90, 180]
            labels: values.map(v => v < 60 ? qsTr("%1 minutes").arg(v) : qsTr("%1 hours").arg(v / 60))
            value: page.settings.suggestionCooldownMinutes
            commit: v => page.settings.setNumber("suggestionCooldownMinutes", v)
        }
        SettingCombo {
            enabled: page.offers
            title: qsTr("Offers per day, at most")
            values: [1, 2, 4, 8]
            labels: values.map(v => String(v))
            value: page.settings.suggestionsPerDay
            comboWidth: 120
            commit: v => page.settings.setNumber("suggestionsPerDay", v)
        }
        Item { objectName: "entry:offers.categories"; Layout.fillWidth: true; implicitHeight: 1 }
        Repeater {
            model: [["build", qsTr("Build errors that keep coming back")], ["driving", qsTr("Driving theory test questions")],
                    ["electronics", qsTr("Electronics: values and calculations")], ["docs", qsTr("Technical documentation")],
                    ["study", qsTr("Studying programming")]]
            SettingSwitch {
                required property var modelData
                enabled: page.offers
                title: modelData[1]
                checked: page.settings.awarenessMuted.indexOf(modelData[0]) < 0
                commit: on => {
                    const muted = page.settings.awarenessMuted.filter(c => c !== modelData[0])
                    if (!on) muted.push(modelData[0])
                    return page.settings.setList("awarenessMuted", muted)
                }
            }
        }
        SettingSwitch {
            entry: "offers.fullscreen"
            enabled: page.offers
            title: qsTr("Also in fullscreen applications")
            hint: qsTr("Off: a fullscreen window (a game, a presentation, a film) is never interrupted.")
            checked: page.settings.awarenessFullscreen
            commit: on => page.settings.setOption("awarenessFullscreen", on)
        }
        SettingRow {
            entry: "offers.stats"
            title: qsTr("Offers in the last 30 days")
            hint: page.win.daemonUp
                  ? qsTr("%1 offers, %2 accepted, %3 declined. A \"Not now\" silences that kind of offer for 6 hours and doubles with each refusal; \"Don't offer this\" — for a month.")
                        .arg(page.stats.shown || 0).arg(page.stats.accepted || 0).arg(page.stats.declined || 0)
                  : qsTr("Shown while the Jarvis service runs.")
        }

        // ---- Jarvis speaks up ----
        SettingGroup { title: qsTr("Jarvis speaks up"); text: qsTr("Hints in notifications follow Plasma's Do Not Disturb.") }
        SettingSwitch {
            objectName: "proactiveSwitch"
            entry: "proactive.speak"
            title: qsTr("Jarvis speaks up by itself")
            hint: qsTr("At most one hint every half hour and never between 23:00 and 7:00 or while you play or watch something: a break after long work, words due for review, a question when you have not talked for a while.")
            checked: page.settings.proactive
            commit: on => page.settings.setOption("proactive", on)
        }
        SettingCombo {
            entry: "proactive.breaks"
            enabled: page.settings.proactive && page.settings.trackActivity
            title: qsTr("Suggest a break after")
            hint: page.settings.trackActivity ? "" : qsTr("Needs \"See which application I'm working in\" (Memory and privacy): that is how Jarvis knows you are at the computer.")
            values: [0, 60, 90, 120]
            labels: [qsTr("Never"), qsTr("1 hour"), qsTr("1.5 hours"), qsTr("2 hours")]
            value: page.settings.breakMinutes
            commit: v => page.settings.setBreakMinutes(v)
        }
        SettingSwitch {
            entry: "proactive.morning"
            enabled: page.settings.proactive
            title: qsTr("Morning plan")
            hint: qsTr("When you sit down in the morning: today's reminders, open tasks and flashcards to review.")
            checked: page.settings.morningSummary
            commit: on => page.settings.setOption("morningSummary", on)
        }

        // ---- Permissions ----
        SettingGroup { title: qsTr("Permissions") }
        SettingSwitch {
            objectName: "integrationSwitch"
            entry: "actions.integration"
            title: qsTr("Let widgets and KRunner use Jarvis")
            hint: qsTr("KRunner answers \"jarvis <question>\", the desktop widgets show Jarvis's state and may start the actions Jarvis lists (open the overlay, tasks, memory, pause observation). No component can run commands or read your memory through it.")
            checked: page.settings.desktopIntegration
            commit: on => page.settings.setOption("desktopIntegration", on)
        }
        SettingSwitch {
            objectName: "shareActiveAppSwitch"
            entry: "actions.activeApp"
            enabled: page.settings.desktopIntegration && page.settings.awarenessWindow
            title: qsTr("Tell desktop components the active application")
            hint: qsTr("Its name only (\"kate\", \"firefox\"), never the window title. Any program of your session could then ask for it — leave it off unless a widget of yours needs it. Needs \"Allow Jarvis to see the active window\".")
            checked: page.settings.shareActiveApp
            commit: on => page.settings.setOption("shareActiveApp", on)
        }
        SettingRow {
            entry: "actions.confirm"
            title: qsTr("What Jarvis always asks before doing")
            hint: qsTr("Before it changes the Plasma desktop (JARVIS DESKTOP, widgets, virtual desktops), deletes saved data, or starts reading the screen, Jarvis shows what will happen and waits for your yes. Commands you give yourself (open an application, switch a workspace, a power profile chosen in KRunner) run at once. Installing a Jarvis update goes through Discover, where the system itself asks for the administrator password: Jarvis never sees or keeps it.")
        }
    }
}
