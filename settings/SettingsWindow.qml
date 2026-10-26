import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Jarvis

// Jarvis settings (0.21): one window with the pages on the left, a search that
// finds a setting by its name, its explanation or a synonym (locally, without
// the internet), and a reset per page. It replaces the Control center dialog
// and the separate Claude API dialog. It is a window of its own: it opens from
// the tray, the dock or `jarvis --open settings[:page]` without the main window,
// and keeps working while jarvisd is stopped (rows that need the service say so).
//
// How changes apply: switches, lists and sliders save at once and the thing
// they change follows at once; text fields that need checking (the API key, the
// model, excluded applications) have their own button; the few settings that
// need a restart say so on their row. A failed save leaves the old value shown
// and says why under the row.
ApplicationWindow {
    id: win
    objectName: "controlCenter"
    required property var desktop
    required property var voice
    required property var chat
    required property var settings
    property var screenAware: null  // ScreenAwareness
    property var shortcut: null     // GlobalShortcut
    signal memoryRequested()

    property string page: "desktop"
    // For the pages' `win` (inside a page, `win` names the page's own property).
    readonly property var host: win
    readonly property bool daemonUp: chat.daemonConnected
    readonly property bool english: settings.uiLanguage === "en"
    readonly property var pages: ["desktop", "conversation", "intelligence", "privacy", "actions", "skills", "system"]

    title: qsTr("Jarvis settings")
    color: Theme.bg
    // The dark palette for every standard control (check boxes, fields, lists, spin boxes).
    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.bg
    palette.alternateBase: Theme.panel
    palette.text: Theme.text
    palette.button: Theme.botBubble
    palette.buttonText: Theme.text
    palette.brightText: "white"
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.bg
    palette.placeholderText: Theme.textDim
    palette.light: Theme.hover
    palette.midlight: Theme.border
    // Field and check box outlines: visible on the dark background.
    palette.mid: Theme.textDim
    palette.dark: Theme.textDim
    palette.shadow: "black"
    palette.toolTipBase: Theme.panel
    palette.toolTipText: Theme.text
    // Fits 1366×768 and the same screen at 125 % and 150 % (911×512 logical).
    width: Math.min(1120, Screen.desktopAvailableWidth - 40)
    height: Math.min(780, Screen.desktopAvailableHeight - 40)
    minimumWidth: Math.min(620, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(420, Screen.desktopAvailableHeight)
    onWidthChanged: SettingsState.narrow = width < 860

    function open() {
        show()
        raise()
        requestActivate()
    }
    // "privacy" or "privacy:awareness.screen" (a page, or a row on it).
    function showPage(target) {
        const parts = String(target || "").split(":")
        if (parts.length > 1 && parts[1].length > 0)
            reveal(parts[1])
        else {
            if (pages.indexOf(parts[0]) >= 0)
                page = parts[0]
            search.text = ""
            open()
        }
    }
    // Shows the page of a catalog entry, scrolls to its row and outlines it.
    function reveal(id) {
        const entry = settings.settingEntry(id)
        if (!entry.page)
            return
        search.text = ""
        page = entry.page
        open()
        SettingsState.highlight = id
        highlightTimer.restart()
        Qt.callLater(scrollTo, id, 0)
    }
    function findEntry(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const found = findEntry(kids[i], name)
            if (found)
                return found
        }
        return null
    }
    function scrollTo(id, attempt) {
        const target = findEntry(pageLoader.item, "entry:" + id)
        // The page may still be laying out right after a switch.
        if (!target || target.height <= 0) {
            if (attempt < 10)
                Qt.callLater(scrollTo, id, attempt + 1)
            return
        }
        const flick = pageScroll.contentItem
        const y = target.mapToItem(flick.contentItem, 0, 0).y
        flick.contentY = Math.max(0, Math.min(y - 24, flick.contentHeight - flick.height))
        // The row's first control gets the keyboard focus.
        const control = firstFocusable(target)
        if (control)
            control.forceActiveFocus(Qt.OtherFocusReason)
    }
    function firstFocusable(item) {
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const k = kids[i]
            if (k.visible && k.enabled && k.activeFocusOnTab)
                return k
            const deeper = firstFocusable(k)
            if (deeper)
                return deeper
        }
        return null
    }
    // Compatibility with the 0.20 Control center's entry points.
    function showVoice() { reveal("voice.speak") }
    function showMemory() { showPage("privacy") }
    function showAwareness() { showPage("privacy") }
    function showApp() { showPage("system") }
    function showDesktop() { showPage("desktop") }
    function showSkill(id, tab) {
        const skill = desktop.skills.find(s => s.id === id)
        if (!skill)
            return
        showPage("skills")
        skillDetails.show(skill)
        if (tab)
            skillDetails.showTab(tab)
    }
    function skillText(skill, field) { return english && skill[field + "_en"] ? skill[field + "_en"] : skill[field] }

    // The reset of a page: first what changes, then the change.
    function askReset(section) {
        resetDialog.section = section
        let lines = settings.resetPreview(section)
        if (section === "conversation" || section === "all")
            lines = lines.concat(voice.resetPreview())
        if ((section === "desktop" || section === "all") && desktopShell.dock.found) {
            const d = desktopShell.dock
            if (d.iconSize !== 62 || d.magnification !== true || d.magnifyAmount !== 35 || d.panelStyle !== true
                    || d.showProfiles !== true || d.showDesktops !== true || d.showJarvis !== true || d.groupWindows !== true)
                lines.push(qsTr("The dock's icons, magnification, glass and buttons: defaults"))
        }
        resetDialog.lines = lines
        resetDialog.open()
    }

    Timer { id: highlightTimer; interval: 3500; onTriggered: SettingsState.highlight = "" }

    // Plasma: the dock, JARVIS DESKTOP, widgets and diagnostics.
    DesktopShell {
        id: desktopShell
        watchDock: win.visible && win.page === "desktop"
        onFinished: (ok, message) => { win.settings.reload(); plasmaBridge.refresh() }
    }
    PlasmaBridge {
        id: plasmaBridge
        onRestored: refreshLater.restart()
    }
    // The service and Plasma need a moment after a restart or a change.
    Timer { id: refreshLater; interval: 1500; onTriggered: plasmaBridge.refresh() }
    KdeSettings { id: kdeSettings }

    Shortcut { sequences: [StandardKey.Find]; onActivated: { search.forceActiveFocus(); search.selectAll() } }
    Shortcut { sequences: [StandardKey.Close]; onActivated: win.close() }
    Shortcut { sequence: "Esc"; enabled: search.text.length > 0; onActivated: search.text = "" }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- Pages and search ----------
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: SettingsState.narrow ? 200 : 248
            color: Theme.panel
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label { text: qsTr("Settings"); color: Theme.text; font.pixelSize: 22; font.bold: true }
                TextField {
                    id: search
                    objectName: "settingsSearch"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search settings")
                    placeholderTextColor: Theme.textDim
                    color: Theme.text
                    selectByMouse: true
                    leftPadding: 12
                    Accessible.name: qsTr("Search settings")
                    background: Rectangle { implicitHeight: 38; radius: 10; color: Theme.bg; border.width: search.activeFocus ? 2 : 1; border.color: search.activeFocus ? Theme.accent : Theme.border }
                    Keys.onDownPressed: if (results.count > 0) { results.forceActiveFocus(); results.currentIndex = 0 }
                    onAccepted: if (results.count > 0) win.reveal(results.model[0].id)
                }
                ListView {
                    id: pageList
                    objectName: "settingsPages"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: win.pages
                    currentIndex: win.pages.indexOf(win.page)
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    spacing: 2
                    Accessible.name: qsTr("Settings pages")
                    Accessible.role: Accessible.List
                    delegate: ItemDelegate {
                        id: pageItem
                        required property string modelData
                        required property int index
                        readonly property bool current: win.page === modelData && search.text.length === 0
                        width: ListView.view.width
                        height: Math.max(42, pageLabel.implicitHeight + 16)
                        text: win.settings.pageTitle(modelData)
                        Accessible.name: text
                        contentItem: Label {
                            id: pageLabel
                            leftPadding: 10
                            text: pageItem.text
                            color: pageItem.current ? Theme.accent : Theme.text
                            font.bold: pageItem.current
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 8
                            color: pageItem.current ? Theme.selected : pageItem.hovered ? Theme.hover : "transparent"
                            border.width: pageItem.ListView.isCurrentItem && pageList.activeFocus ? 2 : 0
                            border.color: Theme.accent
                            // The current page is marked by a bar, not only by colour.
                            Rectangle { visible: pageItem.current; width: 4; radius: 2; height: parent.height - 16; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                        }
                        onClicked: { search.text = ""; win.page = modelData; pageScroll.contentItem.contentY = 0 }
                    }
                    Keys.onReturnPressed: { search.text = ""; win.page = win.pages[currentIndex] }
                    Keys.onSpacePressed: { search.text = ""; win.page = win.pages[currentIndex] }
                }
                Label {
                    Layout.fillWidth: true
                    text: "Jarvis " + win.desktop.version
                    color: Theme.textDim
                    font.pixelSize: 11
                }
            }
        }

        // ---------- The page or the search results ----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // jarvisd stopped: settings still save; what needs the service says so.
            Rectangle {
                objectName: "daemonBanner"
                Layout.fillWidth: true
                visible: !win.daemonUp
                implicitHeight: bannerRow.implicitHeight + 20
                color: "#2a2312"
                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.text
                        text: "⚠ " + qsTr("The Jarvis service (jarvisd) is not running. Settings are still saved and apply when it starts; until then reminders, hints, widgets and KRunner wait, and the service's statistics are not shown.")
                    }
                    DarkButton { text: qsTr("Start the service"); onClicked: { win.desktop.restartDaemon(); refreshLater.restart() } }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 16
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    text: search.text.length > 0 ? qsTr("Search: %1").arg(search.text) : win.settings.pageTitle(win.page)
                    color: Theme.text
                    font.pixelSize: 24
                    font.bold: true
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    Accessible.role: Accessible.Heading
                }
                DarkButton {
                    objectName: "resetPageButton"
                    visible: search.text.length === 0 && win.page !== "skills"
                    text: qsTr("Reset this page…")
                    onClicked: win.askReset(win.page)
                }
            }

            // A line between the heading and what scrolls under it.
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 10; implicitHeight: 1; color: Theme.border }

            // Search results: the setting and its page; Enter or a click opens it.
            ListView {
                id: results
                objectName: "settingsResults"
                visible: search.text.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                clip: true
                spacing: 4
                model: search.text.length > 0 ? win.settings.searchSettings(search.text) : []
                keyNavigationEnabled: true
                Keys.onReturnPressed: if (currentIndex >= 0) win.reveal(model[currentIndex].id)
                Keys.onUpPressed: event => { if (currentIndex <= 0) search.forceActiveFocus(); else event.accepted = false }
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: hit
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    Accessible.name: modelData.title + ", " + modelData.pageTitle
                    contentItem: ColumnLayout {
                        spacing: 2
                        Label { text: hit.modelData.title; color: Theme.text; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label {
                            text: hit.modelData.pageTitle + " › " + hit.modelData.group + (hit.modelData.kcm ? " · " + qsTr("opens KDE System Settings") : "")
                            color: Theme.accent; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap
                        }
                        Label { text: hit.modelData.description; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                    background: Rectangle {
                        radius: 10
                        color: hit.hovered ? Theme.hover : Theme.panel
                        border.width: hit.ListView.isCurrentItem && results.activeFocus ? 2 : 0
                        border.color: Theme.accent
                    }
                    onClicked: win.reveal(modelData.id)
                }
                Label {
                    objectName: "noResults"
                    anchors.centerIn: parent
                    width: parent.width - 40
                    visible: results.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    text: qsTr("Nothing found for \"%1\". Try other words, for example: sound, keys, key, memory, dock, screen.").arg(search.text)
                    textFormat: Text.PlainText
                }
            }

            ScrollView {
                id: pageScroll
                visible: search.text.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                Loader {
                    id: pageLoader
                    width: pageScroll.availableWidth
                    // One page at a time: diagnostics and Plasma reads run only on their page.
                    sourceComponent: ({ desktop: desktopPage, conversation: conversationPage, intelligence: intelligencePage,
                                        privacy: privacyPage, actions: actionsPage, skills: skillsPage, system: systemPage })[win.page]
                    onLoaded: pageScroll.contentItem.contentY = 0
                }
            }
        }
    }

    Component { id: desktopPage; DesktopPage { win: host; shell: desktopShell; plasma: plasmaBridge } }
    Component { id: conversationPage; ConversationPage { win: host; kde: kdeSettings } }
    Component { id: intelligencePage; IntelligencePage { win: host } }
    Component { id: privacyPage; PrivacyPage { win: host } }
    Component { id: actionsPage; ActionsPage { win: host } }
    Component { id: skillsPage; SkillsPage { win: host; skillPage: skillDetails } }
    Component { id: systemPage; SystemPage { win: host; shell: desktopShell; plasma: plasmaBridge } }

    SkillPage {
        id: skillDetails
        desktop: win.desktop
        chat: win.chat
        english: win.english
        onAskRequested: question => { skillDetails.close(); win.close(); win.chat.send(question) }
        onAskWithFilesRequested: (question, files) => { skillDetails.close(); win.close(); win.chat.sendWithFiles(question, files) }
    }

    // What a reset changes, before it changes it. Memory, history, keys and the
    // pinned applications are never part of it.
    ConfirmDialog {
        id: resetDialog
        objectName: "resetDialog"
        property string section
        property var lines: []
        title: section === "all" ? qsTr("Reset all Jarvis settings?") : qsTr("Reset \"%1\"?").arg(win.settings.pageTitle(section))
        notice: lines.length === 0
        acceptText: lines.length > 0 ? qsTr("Reset") : qsTr("Close")
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: resetDialog.lines.length > 0 ? qsTr("These settings return to their defaults:") : qsTr("Everything here already has its default value.")
            }
            Repeater {
                model: resetDialog.lines
                Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "• " + modelData; color: Theme.textDim; textFormat: Text.PlainText }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.textDim
                text: qsTr("Not changed: memory and conversation history, API keys, pinned applications, the desktop layout and JARVIS DESKTOP itself. Permissions are never switched on by a reset, and excluded applications stay excluded.")
            }
        }
        onAccepted: {
            if (resetDialog.lines.length === 0)
                return
            const section = resetDialog.section
            let error = win.settings.resetSection(section)
            if (section === "conversation" || section === "all")
                win.voice.resetSettings()
            if (section === "desktop" || section === "all") {
                desktopShell.resetDockLook()
                // The dock's panel follows the reset values.
                for (const key of ["dockPosition", "dockSize", "dockAlignment", "dockAutohide"])
                    if (!error)
                        error = desktopShell.setDock(key, win.settings.desktop[key])
                desktopShell.update()
            }
            resetResult.text = error ? qsTr("Not everything was reset: %1").arg(error) : qsTr("Done: the settings are back to their defaults.")
            resetResult.open()
        }
    }
    ConfirmDialog {
        id: resetResult
        property alias text: resetLabel.text
        notice: true
        title: qsTr("Reset")
        Label { id: resetLabel; width: parent.width; wrapMode: Text.Wrap }
    }
}
