import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// Settings → System and recovery: start, status and diagnostics, graphics,
// updates, KDE's own settings, reset and the way back.
ColumnLayout {
    id: page
    required property var win
    required property var shell
    required property var plasma
    readonly property var settings: win.settings
    readonly property var desktop: win.desktop
    readonly property var levelColors: ({ ok: Theme.accent, warn: Theme.nodeFact, error: Theme.danger, off: Theme.textDim })
    readonly property var levelMarks: ({ ok: "✓", warn: "!", error: "✗", off: "–" })

    spacing: 6
    // Diagnostics run when the page is shown, not before.
    Component.onCompleted: plasma.refresh()

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        // ---- Start ----
        SettingGroup { title: qsTr("Start") }
        SettingSwitch {
            objectName: "autostartSwitch"
            entry: "system.autostart"
            title: qsTr("Start Jarvis when I log in")
            hint: qsTr("Jarvis waits in the system tray and comes back by itself if it crashes. Closing the window only hides it; \"Quit\" in the tray menu ends it.")
            note: qsTr("Takes effect at the next login.")
            checked: page.desktop.autostart
            commit: on => { page.desktop.autostart = on; return "" }
        }
        SettingSwitch {
            objectName: "backgroundSwitch"
            entry: "system.background"
            title: qsTr("Keep reminders working when the window is closed")
            hint: qsTr("The background service keeps reminders, hints, KRunner and the desktop widgets working after the Jarvis window is closed or quit. Off: the service ends a few seconds after the window; reminders wait until Jarvis is open again.")
            checked: page.settings.background
            commit: on => page.settings.setOption("background", on)
        }

        // ---- Status ----
        SettingGroup { title: qsTr("Status") }
        SettingRow {
            entry: "system.version"
            title: qsTr("Versions and services")
            hint: [qsTr("Jarvis %1, Qt %2").arg(page.desktop.version).arg(page.settings.qtVersion),
                   page.win.daemonUp ? "✓ " + qsTr("Background service (jarvisd): running") : "✗ " + qsTr("Background service (jarvisd): not running"),
                   page.settings.keyringAvailable ? "✓ " + qsTr("System password store: available") : "– " + qsTr("System password store: not available, keys are kept in owner-only files"),
                   page.plasma.plasmaRunning ? "✓ " + qsTr("Plasma: running") : "– " + qsTr("Plasma: not running here")].join("\n")
        }
        SettingRow {
            entry: "system.service"
            title: qsTr("Background service")
            hint: qsTr("Reminders and hints pause while the service is stopped.")
            Flow {
                width: SettingsState.narrow ? page.width - 60 : 330
                spacing: 6
                DarkButton { text: page.win.daemonUp ? qsTr("Restart") : qsTr("Start"); enabled: !page.win.chat.busy; onClicked: { page.desktop.restartDaemon(); refreshLater.restart() } }
                DarkButton { text: qsTr("Stop"); enabled: page.win.daemonUp && !page.win.chat.busy; onClicked: page.win.chat.quitDaemon() }
                DarkButton { text: qsTr("Restart Jarvis"); enabled: !page.win.chat.busy && !page.win.voice.busy && !page.desktop.updating; onClicked: page.desktop.restart() }
            }
        }
        SettingRow {
            entry: "system.diagnostics"
            title: qsTr("Diagnostics")
            hint: page.plasma.diagnostics.length === 0 ? (page.plasma.busy ? qsTr("Checking…") : qsTr("Not checked yet."))
                  : qsTr("The same check in a terminal: jarvis --diagnose · the service log: journalctl --user -u jarvis")
            Row {
                spacing: 6
                DarkButton { text: qsTr("Check again"); enabled: !page.plasma.busy; onClicked: page.plasma.refresh() }
                DarkButton { text: qsTr("Copy report"); enabled: page.plasma.diagnostics.length > 0; onClicked: page.plasma.copyReport() }
            }
        }
        Repeater {
            model: page.plasma.diagnostics
            RowLayout {
                id: diagRow
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                spacing: 10
                // The level is a sign too, not only a colour.
                Label { text: page.levelMarks[diagRow.modelData.level] || "–"; color: page.levelColors[diagRow.modelData.level] || Theme.textDim; font.bold: true; Layout.alignment: Qt.AlignTop }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: diagRow.modelData.name; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { text: diagRow.modelData.text; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                }
            }
        }
        Timer { id: refreshLater; interval: 1500; onTriggered: page.plasma.refresh() }

        // ---- Graphics ----
        SettingCombo {
            objectName: "gpuCombo"
            entry: "system.graphics"
            title: qsTr("Graphics card for Jarvis")
            readonly property var cards: jarvisGraphics.gpus
            readonly property string defaultName: cards.length > 0 ? (cards.find(c => c.boot) || cards[0]).name : ""
            readonly property string otherName: { const o = cards.filter(c => !c.boot); return o.length > 0 ? o[0].name : "" }
            values: ["auto", "integrated", "dedicated"]
            labels: [qsTr("System default"),
                     defaultName.length > 0 ? qsTr("Integrated: %1").arg(defaultName) : qsTr("Integrated"),
                     otherName.length > 0 ? qsTr("Dedicated: %1").arg(otherName) : qsTr("Dedicated (none found)")]
            value: jarvisGraphics.gpuChoice
            comboWidth: 320
            note: jarvisGraphics.restartNeeded ? qsTr("Applies after Jarvis restarts.") : ""
            commit: v => { jarvisGraphics.gpuChoice = v; return "" }
        }
        SettingCombo {
            objectName: "graphicsApiCombo"
            title: qsTr("Graphics API")
            hint: !jarvisGraphics.ready ? qsTr("Renders: measured after the first frame.")
                  : jarvisGraphics.software ? qsTr("Renders: %1 %2 — SOFTWARE rendering on the CPU (%3). Choose the light HUD or the classic chat, or check the graphics driver.").arg(jarvisGraphics.api).arg(jarvisGraphics.apiVersion).arg(jarvisGraphics.renderer)
                  : qsTr("Renders: %1 %2 on %3 (%4).").arg(jarvisGraphics.api).arg(jarvisGraphics.apiVersion).arg(jarvisGraphics.renderingName).arg(jarvisGraphics.renderer)
            values: ["auto", "opengl", "vulkan"]
            labels: [qsTr("Automatic (OpenGL)"), "OpenGL", "Vulkan"]
            value: jarvisGraphics.apiChoice
            note: jarvisGraphics.restartNeeded ? qsTr("Applies after Jarvis restarts.") : ""
            commit: v => { jarvisGraphics.apiChoice = v; return "" }
        }
        Label {
            visible: text.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 10
            wrapMode: Text.Wrap
            color: Theme.danger
            text: [jarvisGraphics.notice].concat(jarvisGraphics.warnings).filter(t => t.length > 0).map(t => "⚠ " + t).join("\n")
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            spacing: 6
            DarkButton {
                visible: jarvisGraphics.restartNeeded
                text: qsTr("Restart Jarvis to apply")
                primary: true
                enabled: !page.win.chat.busy && !page.win.voice.busy && !page.desktop.updating
                onClicked: page.desktop.restart()
            }
            DarkButton { text: qsTr("Copy graphics diagnostics"); onClicked: jarvisGraphics.copyReport() }
        }

        // ---- Updates ----
        SettingRow {
            entry: "system.update"
            title: qsTr("Updates")
            hint: qsTr("From Bohdan99py/jarvis-kubuntu on GitHub. The button downloads a verified package and opens Discover; the system asks for your confirmation and password there.") + (page.desktop.updateStatus ? "\n" + page.desktop.updateStatus : "")
            DarkButton { text: page.desktop.updating ? qsTr("Checking and downloading…") : qsTr("Update Jarvis"); primary: true; enabled: !page.desktop.updating; onClicked: page.desktop.update() }
        }

        // ---- KDE's own settings ----
        SettingGroup {
            title: qsTr("KDE System Settings")
            text: qsTr("Sound, Bluetooth, networks and screens are KDE's: these buttons open the right page of System Settings.")
        }
        KdeLink { entry: "kde.sound"; kcm: "kcm_pulseaudio"; title: qsTr("Sound: speakers, headphones and microphone"); description: qsTr("Opens System Settings → Sound.") }
        KdeLink { entry: "kde.bluetooth"; kcm: "kcm_bluetooth"; title: qsTr("Bluetooth"); description: qsTr("Opens System Settings → Bluetooth.") }
        KdeLink { entry: "kde.network"; kcm: "kcm_networkmanagement"; title: qsTr("Network and Wi-Fi"); description: qsTr("Opens System Settings → Wi-Fi & Networking.") }
        KdeLink { entry: "kde.displays"; kcm: "kcm_kscreen"; title: qsTr("Displays and scale"); description: qsTr("Opens System Settings → Display Configuration.") }
        KdeLink { entry: "kde.power"; kcm: "kcm_powerdevilprofilesconfig"; title: qsTr("Power and battery"); description: qsTr("Opens System Settings → Power Management.") }
        KdeLink { entry: "kde.appearance"; kcm: "kcm_lookandfeel"; title: qsTr("Global theme and colours of KDE"); description: qsTr("Opens System Settings → Global Theme.") }
        KdeLink { entry: "kde.keyboard"; kcm: "kcm_keyboard"; title: qsTr("Keyboard layouts"); description: qsTr("Opens System Settings → Keyboard.") }
        KdeLink { entry: "kde.region"; kcm: "kcm_regionandlang"; title: qsTr("Region and language of KDE"); description: qsTr("Opens System Settings → Region & Language.") }
        KdeLink { entry: "kde.autostart"; kcm: "kcm_autostart"; title: qsTr("Autostart of other applications"); description: qsTr("Opens System Settings → Autostart.") }

        // ---- Recovery ----
        SettingGroup { title: qsTr("Recovery") }
        SettingRow {
            entry: "system.reset"
            title: qsTr("Reset Jarvis settings")
            hint: qsTr("Every page has \"Reset this page…\". This button resets all pages at once. Before anything changes you see the list; memory, conversation history, API keys and pinned applications always stay.")
            DarkButton { objectName: "resetAllButton"; text: qsTr("Reset all settings…"); onClicked: page.win.askReset("all") }
        }
        SettingRow {
            entry: "system.restore"
            title: qsTr("Back to the standard KDE desktop")
            hint: qsTr("Puts Plasma's layout back exactly as it was before JARVIS DESKTOP (from the backup), or — without a backup — removes every Jarvis widget and panel. Wallpaper, themes and KDE settings are not touched. If Jarvis itself does not start: jarvis --restore-desktop")
            DarkButton { objectName: "restoreDesktopButton"; text: qsTr("Restore…"); enabled: !page.plasma.busy && !page.shell.busy; onClicked: restoreConsent.open() }
        }
        SettingRow {
            entry: "system.files"
            title: qsTr("Settings files and going back to an older version")
            hint: qsTr("Settings: %1. Backup made before the 0.21 upgrade: %2 (without keys).").arg(page.settings.configPath).arg(page.settings.backupPath)
                  + "\n" + qsTr("Before installing a Jarvis older than 0.21, run in a terminal: jarvis --prepare-downgrade — it writes the keys back into the files the older version reads. Without it the older version asks for the keys again; nothing else is lost.")
                  + (settingsMigration.length > 0 ? "\n" + qsTr("Last upgrade of the settings: %1").arg(settingsMigration) : "")
        }
    }

    ConfirmDialog {
        id: restoreConsent
        width: Math.min(500, page.win.width - 40)
        title: qsTr("Back to the standard KDE desktop?")
        acceptText: qsTr("Restore")
        destructive: true
        Label {
            width: parent.width; wrapMode: Text.Wrap
            text: page.shell.hasBackup ? qsTr("plasmashell restarts for a moment and Plasma's layout comes back from the backup made before JARVIS DESKTOP (%1). Changes you made to panels and widgets since then are saved aside, not kept. Your applications keep running.").arg(page.shell.backupPath)
                                       : qsTr("There is no backup: every Jarvis widget and panel is removed; nothing else changes.")
        }
        onAccepted: page.shell.restore()
    }
}
