import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis
import Jarvis.Core
import Qt.labs.platform as Platform
import QtQuick.Dialogs as Dialogs

ApplicationWindow {
    id: root

    width: 1440
    height: 880
    minimumWidth: 1000
    minimumHeight: 700
    visible: !startQuick && !startTray && !startOverlay && startOpen !== "study"
    title: qsTr("J.A.R.V.I.S.")
    color: hudOn ? JarvisTheme.void_ : Theme.bg

    // ---------- Cinematic HUD ----------
    // "off" keeps the classic window; low, balanced and cinematic trade detail for GPU time.
    // The HUD needs shaders: with Qt's software backend Jarvis keeps the classic window.
    readonly property bool hudOn: appSettings.hudQuality !== "off" && jarvisGraphics.shadersSupported
    readonly property bool shownNow: root.visible && root.visibility !== Window.Minimized
    property bool hudDemo: hudForcedState === "demo"
    property string lastAsked: ""
    // Chat effects (unfolding cards, pulses) off in the Low quality and for software rendering.
    readonly property bool chatEffects: appSettings.hudQuality !== "low" && !(jarvisGraphics.ready && jarvisGraphics.software)
    readonly property var awareness: { try { return JSON.parse(chatModel.context) } catch (e) { return {} } }
    readonly property bool awarenessShown: (!!awareness.available || (awareness.pausedUntil || 0) > 0 || screenAware.capturing || screenAware.background)
                                           && appSettings.awarenessLevel !== "off"

    VisualStateController {
        id: visualState
        busy: chatModel.busy
        speaking: voiceEngine.speaking
        listening: voiceEngine.recording || study.listening
        jobs: (voiceEngine.busy && !voiceEngine.recording ? 1 : 0) + (study.busy ? 1 : 0) + (desktopBridge.updating ? 1 : 0)
        demo: root.hudDemo
        forcedState: hudForcedState
    }
    HudTelemetry {
        id: telemetry
        window: root
        graphics: jarvisGraphics
        active: root.hudOn && root.shownNow
    }
    HudController {
        id: hudCtl
        running: root.hudOn && root.shownNow
        refreshRate: jarvisGraphics.refreshRate
        quality: root.hudOn ? appSettings.hudQuality : "low"
        hudState: visualState.state
        level: voiceEngine.outputLevel
        bands: voiceEngine.outputBands
        levelMeasured: voiceEngine.outputMeasured
        micLevel: Math.max(voiceEngine.inputLevel, study.listening ? study.level : 0)
        streamRate: visualState.streamRate
        streaming: visualState.streaming
    }
    // ---------- Overlay, screen awareness and the global shortcut ----------
    // The overlay is a layer-shell surface on KDE Wayland (when the QML module
    // exists), otherwise an always-on-top window. Created once and kept: the
    // conversation and its context survive closing and reopening it.
    property var overlay: null
    Component { id: plainOverlay; HudOverlay {} }
    function createOverlay() {
        const props = { vs: visualState, voice: voiceEngine, chat: chatModel, screenAware: screenAware,
                        settings: appSettings, graphicsInfo: jarvisGraphics }
        let component = null
        if (desktopBridge.layerShell) {
            component = Qt.createComponent("qrc:/jarvis-runtime/OverlayLayer.qml")
            if (component.status !== Component.Ready) {
                console.info("Jarvis: plain overlay window instead of layer shell: " + component.errorString())
                component = null
            }
        }
        const o = (component || plainOverlay).createObject(root, props)
        o.quality = Qt.binding(() => appSettings.hudQuality)
        o.wanted = Qt.binding(() => appSettings.overlay)
        o.openRequested.connect(root.showMain)
        o.closeRequested.connect(() => appSettings.overlay = false)
        o.voiceSetupRequested.connect(() => { root.showMain(); controlCenter.showVoice() })
        root.overlay = o
    }
    function showOverlay(token, prompt) { if (root.overlay) root.overlay.showOverlay(token, prompt || "") }
    ScreenAwareness {
        id: screenAware
        allowed: appSettings.awarenessScreen && appSettings.awarenessLevel !== "off"
        cloudImages: appSettings.awarenessCloudImages
        intervalMinutes: appSettings.screenIntervalMinutes
        level: appSettings.awarenessLevel
        paused: (root.awareness.pausedUntil || 0) > 0
        onCapturingChanged: if (capturing) visualState.record("tool", qsTr("screen awareness"))
    }
    GlobalShortcut {
        id: globalShortcut
        onActivated: token => root.overlay && root.overlay.open ? root.overlay.hideOverlay() : root.showOverlay(token, "")
    }
    // Real events feed the state machine; it hands every event (and the demo's,
    // marked as such) to the HUD's effects.
    Connections {
        target: chatModel
        function onActivity(kind, detail) { visualState.record(kind, detail) }
    }
    Connections {
        target: visualState
        function onPulse(kind, detail, demo) { if (root.hudOn) hudStage.event(kind, detail) }
    }
    // Background jobs are tools at work.
    readonly property bool voiceJob: voiceEngine.busy && !voiceEngine.recording
    onVoiceJobChanged: if (voiceJob) visualState.record("tool", voiceEngine.status)
    readonly property bool studyJob: study.busy
    onStudyJobChanged: if (studyJob) visualState.record("tool", study.status)
    readonly property bool updateJob: desktopBridge.updating
    onUpdateJobChanged: if (updateJob) visualState.record("tool", qsTr("update check"))

    // The space behind everything: grid, haze, scan line and the waves the core sends out.
    // Split by how often each part changes, because it covers the whole window:
    Item {
        id: field
        anchors.fill: parent
        visible: root.hudOn
        readonly property point centre: {
            const p = hudStage.mapToItem(root.contentItem, hudStage.coreCentre.x, hudStage.coreCentre.y)
            return Qt.point(p.x / Math.max(1, width), p.y / Math.max(1, height))
        }
        readonly property color accent: hudCtl.colorA
        // the still grid, cached: drawn again only when its colour or the size changes;
        ShaderEffect {
            anchors.fill: parent
            layer.enabled: true
            property size size: Qt.size(width, height)
            property point centre: field.centre
            property real coreRadius: hudStage.coreRadius
            property color accent: field.accent
            property color alertColor: hudCtl.alertColor
            property real alertMix: hudCtl.alertMix
            property real detail: hudCtl.detail
            fragmentShader: "hud/shaders/field.frag.qsb"
        }
        // haze and waves, drawn additively over it. (Rendering them into a smaller
        // texture was tried: every texture update made Qt draw one more frame.)
        ShaderEffect {
            anchors.fill: parent
            visible: hudCtl.detail > 1.5 || hudCtl.waves.some(w => w >= 0)
            property real time: hudCtl.time
            property size size: Qt.size(width, height)
            property point centre: field.centre
            property real coreRadius: hudStage.coreRadius
            property color accent: field.accent
            property color alertColor: hudCtl.alertColor
            property real alertMix: hudCtl.alertMix
            property real energy: hudCtl.energy
            property vector4d waves: Qt.vector4d(hudCtl.waves[0], hudCtl.waves[1], hudCtl.waves[2], hudCtl.waves[3])
            property real detail: hudCtl.detail
            fragmentShader: "hud/shaders/fieldfx.frag.qsb"
        }
        // and the scan line, a thin gradient that moves down the window.
        Rectangle {
            width: parent.width
            height: 120
            y: (hudCtl.time * 0.045 % 1) * (parent.height + 200) - 100 - height
            opacity: 0.6 * hudCtl.scan
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.97; color: Qt.rgba(field.accent.r, field.accent.g, field.accent.b, 0.03) }
                GradientStop { position: 1.0; color: Qt.rgba(field.accent.r, field.accent.g, field.accent.b, 0.12) }
            }
        }
    }

    ChatModel {
        id: chatModel
        objectName: "chatModel"
        // Memory and the graph only need to follow background changes while shown.
        liveUpdates: (root.visible && root.visibility !== Window.Minimized) || quickBar.visible || (!!root.overlay && root.overlay.open)
        screen: screenAware
    }

    VoiceController { id: voiceEngine; language: appSettings.uiLanguage }
    StudyController { id: study; language: appSettings.uiLanguage; onMemoryLearned: chatModel.refreshMemory() }
    StudyWindow { id: studyWindow; objectName: "studyWindow"; study: study; onApiSettingsRequested: { root.show(); root.raise(); settingsDialog.open() } }
    QuickBar { id: quickBar; objectName: "quickBar"; chat: chatModel; voice: voiceEngine; desktop: desktopBridge; settings: appSettings; onVoiceSetupRequested: { quickBar.hide(); root.show(); controlCenter.showVoice() } }
    ControlCenter { id: controlCenter; objectName: "controlCenter"; desktop: desktopBridge; voice: voiceEngine; chat: chatModel; settings: appSettings; screen: screenAware; shortcut: globalShortcut; onApiSettingsRequested: settingsDialog.open() }
    Platform.SystemTrayIcon {
        id: tray
        Component.onCompleted: visible = true
        icon.source: "qrc:/icons/org.jarvis.Jarvis.svg"
        tooltip: qsTr("Jarvis — personal assistant")
        onActivated: quickBar.reveal()
        menu: Platform.Menu {
            Platform.MenuItem { text: globalShortcut.bound ? qsTr("Ask Jarvis (overlay) · %1").arg(globalShortcut.trigger) : qsTr("Ask Jarvis (overlay)"); onTriggered: root.showOverlay("", "") }
            Platform.MenuItem { text: qsTr("Quick commands"); onTriggered: quickBar.reveal() }
            Platform.MenuItem { text: qsTr("Open Jarvis"); onTriggered: { root.show(); root.raise(); root.requestActivate() } }
            Platform.MenuItem { text: qsTr("French studies"); onTriggered: studyWindow.showTab(0) }
            Platform.MenuItem { text: qsTr("Compact overlay"); checkable: true; checked: appSettings.overlay; onTriggered: appSettings.overlay = !appSettings.overlay }
            Platform.MenuItem {
                visible: appSettings.awarenessLevel !== "off"
                text: (root.awareness.pausedUntil || 0) > 0 ? qsTr("Resume observation") : qsTr("Pause observation for 1 hour")
                onTriggered: chatModel.pauseAwareness((root.awareness.pausedUntil || 0) > 0 ? 0 : 60)
            }
            Platform.MenuItem { text: qsTr("Quit"); onTriggered: Qt.quit() }
        }
    }
    // At login Jarvis may start before the panel: show the icon once the tray exists.
    Timer {
        property int tries: 0
        interval: 3000
        repeat: true
        running: !tray.available && tries < 60
        // `available` may not notify: check it on every tick and stop once it is there.
        onTriggered: { if (tray.available) { tray.visible = false; tray.visible = true; tries = 60 } else tries++ }
    }
    Connections {
        target: desktopBridge
        function onQuickRequested() { quickBar.reveal() }
        function onOverlayRequested(token) { root.showOverlay(token, "") }
        function onMainRequested() { root.show(); root.raise(); root.requestActivate() }
    }
    Connections {
        target: voiceEngine
        function onTextReady(text) { if (!quickBar.visible && !(root.overlay && root.overlay.open)) composer.setText(text) }
    }
    Connections {
        target: chatModel
        function onAssistantReply(text) {
            voiceEngine.speak(text)
            // Asked and switched away: the answer comes as a notification.
            if (!root.userSeesChat)
                root.notifyReady("reply", "Jarvis", text, [["default", ""], ["open", qsTr("Open chat")]])
        }
        function onOpenRequested(target, payload) { root.openTarget(target, payload) }
    }

    // ---------- Notifications ----------
    readonly property bool userSeesChat: (root.visible && root.active) || quickBar.visible || (!!root.overlay && root.overlay.open)
    function showMain() { root.show(); root.raise(); root.requestActivate() }
    // Where a notification leads: "main", "tasks", "study", "curious" or "memory".
    function openTarget(target, payload) {
        if (target === "study") {
            studyWindow.showTab(2)
            return
        }
        // An offer of help the user accepted (or a KRunner question): answered in the overlay, with the context.
        if (target === "assist") {
            root.showOverlay("", payload)
            return
        }
        // From the desktop widget and other Plasma components (org.jarvis.Integration1).
        if (target === "overlay") {
            root.showOverlay("", "")
            return
        }
        if (target === "quick") {
            quickBar.reveal()
            return
        }
        showMain()
        if (target === "tasks")
            memoryPanel.showTab(1)
        else if (target === "memory")
            memoryPanel.showTab(2)
        else if (target === "curious") {
            if (payload.length > 0)
                chatModel.say(payload)
            else
                chatModel.askMeSomething()
            composer.focusInput()
        }
    }
    // Something finished while Jarvis was out of sight.
    function notifyReady(key, title, body, actions) {
        if (appSettings.notifyReplies && body.length > 0) {
            notifier.show(key, title, body, actions || [["default", ""]])
            visualState.record("notice", title + ": " + body.replace(/`/g, "").replace(/^#+\s*/gm, "").replace(/\s+/g, " ").slice(0, 160))
        }
    }
    Connections {
        target: notifier
        function onActionInvoked(key, action) {
            if (key === "update") {
                root.showMain()
                controlCenter.showApp()
                if (action === "update")
                    desktopBridge.update()
            } else if (key === "study-job") {
                studyWindow.show(); studyWindow.raise(); studyWindow.requestActivate()
            } else if (key === "voice") {
                root.showMain()
                controlCenter.showVoice()
            } else {
                root.showMain()
                composer.focusInput()
            }
        }
    }
    Connections {
        target: desktopBridge
        function onOpenRequested(target, payload) { root.openTarget(target, payload) }
        function onUpdateAvailable(version) {
            notifier.show("update", qsTr("Jarvis %1 is available").arg(version),
                          qsTr("The package is verified against GitHub's checksum before Discover opens."),
                          [["update", qsTr("Update")], ["default", ""]])
        }
    }
    // Long jobs (transcription, notes, PDF, setup): a notification when they end out of sight.
    Connections {
        target: study
        property real startedAt: 0
        function onStatusChanged() {
            if (study.busy) {
                if (startedAt === 0) startedAt = Date.now()
                return
            }
            const slow = startedAt > 0 && Date.now() - startedAt > 15000
            startedAt = 0
            if (slow && !studyWindow.active)
                root.notifyReady("study-job", qsTr("French studies"), study.status)
        }
    }
    Connections {
        target: voiceEngine
        property real startedAt: 0
        function onChanged() {
            if (voiceEngine.busy) {
                if (startedAt === 0) startedAt = Date.now()
                return
            }
            const slow = startedAt > 0 && Date.now() - startedAt > 20000
            startedAt = 0
            if (slow && !controlCenter.visible)
                root.notifyReady("voice", qsTr("Voice"), voiceEngine.status)
        }
    }

    AppSettings {
        id: appSettings
        onSaved: { chatModel.reloadConfig(); study.reloadConfig() }
        onLanguageApplied: chatModel.retranslate()
    }

    SettingsDialog {
        id: settingsDialog
        settings: appSettings
    }

    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: Qt.quit()
    }
    Shortcut {
        sequence: "Ctrl+M"
        enabled: root.hudOn
        onActivated: appSettings.memoryOpen = !appSettings.memoryOpen
    }
    Shortcut {
        sequence: "Ctrl+Shift+O"
        onActivated: appSettings.overlay = !appSettings.overlay
    }
    // Inside Jarvis the overlay key works even before the desktop bound it globally.
    Shortcut {
        sequence: "Ctrl+J"
        onActivated: root.showOverlay("", "")
    }

    Dialogs.FileDialog {
        id: attachDialog
        title: qsTr("Attach files")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Images, PDF, code, schematics (*.png *.jpg *.jpeg *.gif *.webp *.pdf *.kicad_sch *.kicad_pcb *.kicad_pro *.net *.sim1 *.simu *.c *.h *.cpp *.hpp *.ino *.py *.js *.qml *.json *.txt *.md *.csv *.log *.sh *.cmake *.v *.vhd *.s *.ld *.rs *.go *.cir *.ioc CMakeLists.txt Makefile)"),
                      qsTr("All files (*)")]
        onAccepted: composer.attach(selectedFiles)
    }

    Connections {
        target: chatModel
        function onBusyChanged() {
            if (!chatModel.busy && root.active)
                composer.focusInput()
        }
    }

    Component.onCompleted: {
        createOverlay()
        chatModel.refreshContext()
        if (startOpen.length > 0) openTarget(startOpen, "")
        else if (startOverlay) showOverlay("", "")
        else if (startQuick) quickBar.reveal()
        else if (!startTray) composer.focusInput()
    }
    // Closing the window keeps Jarvis running in the tray (lessons keep recording).
    onClosing: { if(desktopBridge.takeTrayHint()) tray.showMessage(qsTr("Jarvis keeps running"), qsTr("Jarvis stays in the system tray. Click the icon to open it; \"Quit\" in its menu ends it.")) }

    // ---------- Main menu ----------
    Menu {
        id: mainMenu
        width: 300
        padding: 6

        background: Rectangle {
            radius: 10
            color: Theme.panel
            border.width: 1
            border.color: Theme.border
        }

        // French studies and quick commands have their own buttons in the header.
        DarkMenuItem { text: qsTr("Control center…"); onTriggered: controlCenter.open() }
        DarkMenuItem {
            text: qsTr("Clear chat")
            onTriggered: chatModel.clear()
        }
        MenuSeparator {
            contentItem: Rectangle {
                implicitHeight: 1
                color: Theme.border
            }
        }
        DarkMenuItem {
            text: qsTr("Quit")
            onTriggered: Qt.quit()
        }
    }

    RowLayout {
      anchors.fill: parent
      anchors.margins: root.hudOn ? 14 : 20
      spacing: root.hudOn ? 12 : 20
      Item {
        id: chatColumn
        Layout.fillWidth: !root.hudOn
        Layout.preferredWidth: root.hudOn ? Math.max(360, Math.min(480, root.width * 0.31)) : -1
        Layout.fillHeight: true
        // With the HUD the chat is a glass panel over the field.
        Rectangle {
            anchors.fill: parent
            visible: root.hudOn
            radius: 6
            color: JarvisTheme.glass
            border.color: JarvisTheme.line
        }
      ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.hudOn ? 1 : 0
        spacing: 0

        // ---------- Header ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.panel

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    radius: 7
                    color: Theme.accent

                    // Pulses only while thinking: an endless animation makes the
                    // window redraw every frame, even when nothing else changes.
                    SequentialAnimation on opacity {
                        running: chatModel.busy
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.35; duration: 350; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 350; easing.type: Easing.InOutSine }
                    }
                }

                ColumnLayout {
                    spacing: 0

                    Label {
                        text: "J.A.R.V.I.S."
                        color: Theme.text
                        font.pixelSize: 17
                        font.bold: true
                        font.letterSpacing: 3
                    }
                    Label {
                        text: chatModel.busy
                              ? qsTr("thinking…")
                              : (chatModel.daemonConnected ? qsTr("jarvisd connected") : qsTr("local mode"))
                                + (appSettings.hasKey ? qsTr(" · Claude") : "")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                DarkButton {
                    // Beside the HUD the chat is narrow: a shorter label.
                    Layout.preferredWidth: root.hudOn ? implicitContentWidth + 28 : -1
                    text: root.hudOn ? (study.vocabStats.due > 0 ? "FR · " + study.vocabStats.due : "FR")
                                     : (study.vocabStats.due > 0 ? qsTr("Français · %1").arg(study.vocabStats.due) : qsTr("Français"))
                    onClicked: studyWindow.showTab(0)
                    ToolTip.visible: hovered
                    ToolTip.text: study.recording ? qsTr("Recording a lesson…") : qsTr("French studies: lessons, translation, flashcards")
                    Rectangle { visible: study.recording; width: 10; height: 10; radius: 5; color: Theme.danger; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2 }
                }
                DarkButton {
                    Layout.preferredWidth: root.hudOn ? 46 : -1
                    text: "⌕"
                    onClicked: quickBar.reveal()
                    Accessible.name: qsTr("Quick commands")
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Quick commands · Meta+J")
                }
                DarkButton {
                    id: menuBtn
                    Layout.preferredWidth: root.hudOn ? 46 : -1
                    text: root.hudOn ? "☰" : qsTr("☰  Menu")
                    Accessible.name: qsTr("Menu")
                    onClicked: mainMenu.popup(menuBtn, menuBtn.width - mainMenu.width, menuBtn.height + 4)
                }
            }
        }

        // What Jarvis knows about the activity, and the screen eye.
        AwarenessIndicator {
            Layout.leftMargin: 12
            Layout.topMargin: 6
            visible: root.awarenessShown
            chat: chatModel
            screen: screenAware
            onClicked: controlCenter.showAwareness()
        }

        // ---------- Messages ----------
        DropArea {
            id: fileDrop
            Layout.fillWidth: true
            Layout.fillHeight: true
            keys: ["text/uri-list"]
            onDropped: drop => { if (drop.hasUrls) { composer.attach(drop.urls); drop.accept() } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 8
                z: 2
                visible: fileDrop.containsDrag
                radius: 4
                color: Qt.rgba(0.31, 0.85, 0.83, 0.10)
                border.color: JarvisTheme.cyan
                border.width: 2
                Label { anchors.centerIn: parent; text: qsTr("Drop files to attach them"); color: JarvisTheme.cyan; font.pixelSize: 18 }
            }
            ChatView {
                id: chatView
                anchors.fill: parent
                chat: chatModel
                vs: visualState
                effects: root.chatEffects
            }
        }

        // ---------- Input ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: composer.implicitHeight + 22
            color: root.hudOn ? "transparent" : Theme.panel
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: root.hudOn ? JarvisTheme.line : Theme.border }
            ChatComposer {
                id: composer
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 12
                chat: chatModel
                voice: voiceEngine
                effects: root.chatEffects
                onAttachRequested: attachDialog.open()
                onVoiceSetupRequested: controlCenter.showVoice()
                onSent: text => root.lastAsked = text
            }
        }
      }
      }
      HudStage {
        id: hudStage
        objectName: "hudStage"
        visible: root.hudOn
        Layout.fillWidth: root.hudOn
        Layout.fillHeight: true
        ctl: hudCtl
        vs: visualState
        chat: chatModel
        voice: voiceEngine
        telemetry: telemetry
        graphics: jarvisGraphics
        settings: appSettings
        study: study
        asked: root.lastAsked
        memoryOpen: appSettings.memoryOpen
        overlayOn: appSettings.overlay
        demo: root.hudDemo
        onToggleMemory: appSettings.memoryOpen = !appSettings.memoryOpen
        onToggleOverlay: appSettings.overlay = !appSettings.overlay
        onToggleDemo: root.hudDemo = !root.hudDemo
        onQualityPicked: quality => appSettings.hudQuality = quality
        onCoreActivated: {
            if (voiceEngine.ready && !voiceEngine.busy) voiceEngine.listen()
            else if (voiceEngine.recording) voiceEngine.stop()
            else composer.focusInput()
        }
      }
      MemoryPanel {
        id: memoryPanel
        objectName: "memoryPanel"
        // Beside the HUD it folds away on demand (header button, Ctrl+M).
        Layout.preferredWidth: root.hudOn ? (appSettings.memoryOpen ? Math.max(340, root.width * 0.27) : 0) : Math.max(340, root.width * 0.38)
        Behavior on Layout.preferredWidth { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
        visible: Layout.preferredWidth > 2
        clip: true
        Layout.fillHeight: true
        chat: chatModel
        settings: appSettings
        desktop: desktopBridge
        onPrivacyRequested: controlCenter.showMemory()
      }

    }
}
