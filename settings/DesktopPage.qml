import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Jarvis

// Settings → Desktop: JARVIS DESKTOP, the look, the Jarvis window, the dock,
// panels and widgets, workspaces.
ColumnLayout {
    id: page
    required property var win
    required property var shell
    required property var plasma
    readonly property var settings: win.settings
    readonly property var look: settings.desktop
    readonly property var dock: shell.dock
    readonly property bool dockHere: !!dock.found
    function report(error) { return error || "" }
    // A panel setting: Plasma's value when the dock exists, otherwise the saved one.
    function panelValue(key, fallback) { return dockHere && dock[key] !== undefined ? dock[key] : (look[key] !== undefined ? look[key] : fallback) }

    spacing: 6
    Component.onCompleted: plasma.refresh()

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        wrapMode: Text.Wrap
        color: Theme.textDim
        text: qsTr("How Jarvis looks on your Plasma desktop. Changes apply at once; nothing in Plasma changes without your confirmation, and \"Back to standard KDE\" takes every Jarvis addition away.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 14
        Layout.bottomMargin: 24
        spacing: 2

        // ---- JARVIS DESKTOP ----
        SettingGroup { title: qsTr("JARVIS DESKTOP") }
        SettingRow {
            entry: "desktop.mode"
            title: shell.mode === "jarvis" ? qsTr("JARVIS DESKTOP is on") : qsTr("Standard KDE desktop")
            hint: qsTr("Your Plasma desktop in the Jarvis look: the core on the desktop, the Jarvis Dock with your applications, widgets and workspaces. Jarvis checks Plasma, backs up the layout, shows what will change and changes it only after you confirm.")
            ColumnLayout {
                spacing: 6
                DarkButton {
                    objectName: "desktopCheckButton"
                    text: shell.busy && !shell.checked ? qsTr("Checking…") : qsTr("Check compatibility")
                    enabled: !shell.busy
                    onClicked: shell.check()
                }
                DarkButton {
                    objectName: "desktopApplyButton"
                    text: qsTr("Turn on JARVIS DESKTOP…")
                    primary: true
                    visible: shell.mode !== "jarvis"
                    enabled: !shell.busy && shell.checked && shell.compatible
                    onClicked: applyConsent.open()
                }
                DarkButton {
                    objectName: "desktopRestoreButton"
                    text: qsTr("Back to standard KDE…")
                    enabled: !shell.busy && (shell.mode === "jarvis" || shell.hasBackup)
                    onClicked: restoreConsent.open()
                }
            }
        }
        Repeater {
            model: shell.checks
            RowLayout {
                id: checkRow
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Label { text: checkRow.modelData.ok ? "✓" : "✗"; color: checkRow.modelData.ok ? Theme.accent : Theme.danger; font.bold: true }
                Label { text: checkRow.modelData.name + " — " + checkRow.modelData.text; color: Theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            }
        }
        Label { visible: shell.plan.length > 0; Layout.leftMargin: 10; text: qsTr("What will change:"); color: Theme.text; font.bold: true }
        Repeater {
            model: shell.plan
            Label { required property string modelData; Layout.leftMargin: 10; text: "• " + modelData; color: Theme.textDim; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        }
        Label { visible: shell.status.length > 0; Layout.leftMargin: 10; text: shell.status; color: Theme.accent; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }

        // ---- Appearance ----
        SettingGroup { title: qsTr("Appearance"); text: qsTr("Shared by the desktop widgets, the dock and the Jarvis window.") }
        // The look, as the desktop will show it: the real core with the chosen theme.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            radius: 10
            color: JarvisTheme.void_
            border.color: JarvisTheme.line
            clip: true
            Accessible.ignored: true
            HudController {
                id: previewCtl
                running: page.win.visible
                quality: page.look.quality || "balanced"
                hudState: "idle"
                maxRate: 24
            }
            QtObject {
                id: previewVs
                readonly property string state: "idle"
                readonly property bool demo: false
                readonly property var routes: []
                readonly property var counters: ({ requests: 0, errors: 0 })
                readonly property bool streaming: false
                readonly property real streamRate: 0
                readonly property real lastLatency: 0
            }
            JarvisCore {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -12
                width: 130; height: 130
                ctl: previewCtl
                vs: previewVs
                compact: true
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                width: dockRow.implicitWidth + 20
                height: Math.max(26, page.panelValue("dockSize", 44) * 0.6)
                radius: 8
                color: JarvisTheme.glass
                border.color: JarvisTheme.line
                Row {
                    id: dockRow
                    anchors.centerIn: parent
                    spacing: 6
                    Repeater {
                        model: ["GENERAL", "STUDY", "WORK", "DEV"]
                        Rectangle {
                            required property string modelData
                            required property int index
                            width: chipText.implicitWidth + 12; height: 18; radius: 5
                            color: "transparent"
                            border.color: index === 0 ? JarvisTheme.amber : JarvisTheme.line
                            Text { id: chipText; anchors.centerIn: parent; text: modelData; color: JarvisTheme.text; font.pixelSize: 9 }
                        }
                    }
                    Repeater {
                        model: 4
                        Rectangle { width: 18; height: 18; radius: 9; color: Qt.rgba(JarvisTheme.cyan.r, JarvisTheme.cyan.g, JarvisTheme.cyan.b, 0.25); border.color: JarvisTheme.cyan }
                    }
                }
            }
        }
        SettingCombo {
            objectName: "desktopThemeCombo"
            entry: "desktop.theme"
            title: qsTr("Theme")
            values: ["classic", "mark7", "minimal"]
            labels: [qsTr("Classic JARVIS — blue HUD, restrained"), qsTr("Mark VII — amber, more rings, expressive"), qsTr("Minimal — calm, few effects")]
            value: page.look.theme || "classic"
            comboWidth: 320
            commit: v => page.settings.setDesktop("theme", v)
        }
        SettingRow {
            entry: "desktop.colors"
            title: qsTr("Primary and secondary colour")
            hint: qsTr("Your own colours instead of the theme's.")
            Row {
                spacing: 8
                Button {
                    width: 38; height: 38
                    Accessible.name: qsTr("Primary colour")
                    background: Rectangle { radius: 8; color: JarvisTheme.primary; border.width: parent.visualFocus ? 2 : 1; border.color: parent.visualFocus ? Theme.accent : Theme.border }
                    onClicked: { colourDialog.key = "primary"; colourDialog.selectedColor = JarvisTheme.primary; colourDialog.open() }
                }
                Button {
                    width: 38; height: 38
                    Accessible.name: qsTr("Secondary colour")
                    background: Rectangle { radius: 8; color: JarvisTheme.secondary; border.width: parent.visualFocus ? 2 : 1; border.color: parent.visualFocus ? Theme.accent : Theme.border }
                    onClicked: { colourDialog.key = "secondary"; colourDialog.selectedColor = JarvisTheme.secondary; colourDialog.open() }
                }
                DarkButton {
                    text: qsTr("Theme colours")
                    onClicked: { page.settings.setDesktop("primary", ""); page.settings.setDesktop("secondary", "") }
                }
            }
        }
        SettingSlider {
            entry: "desktop.glow"
            title: qsTr("Glow")
            hint: qsTr("How bright the holographic edges shine.")
            from: 0; to: 1.5
            value: page.look.glow !== undefined ? page.look.glow : 1
            commit: v => page.settings.setDesktop("glow", v)
        }
        SettingSlider {
            entry: "desktop.opacity"
            title: qsTr("Panel transparency")
            hint: qsTr("Left: more solid; right: more of the wallpaper shows through.")
            from: 0.95; to: 0.3
            value: page.look.opacity !== undefined ? page.look.opacity : 0.72
            valueText: v => Math.round((1 - v) * 100) + " %"
            commit: v => page.settings.setDesktop("opacity", v)
        }
        SettingSlider {
            entry: "desktop.motion"
            title: qsTr("Animation speed")
            from: 0.5; to: 1.5
            value: page.look.motion !== undefined ? page.look.motion : 1
            commit: v => page.settings.setDesktop("motion", v)
        }
        SettingCombo {
            entry: "desktop.density"
            title: qsTr("Size of Jarvis elements")
            hint: qsTr("Spacing and size in the desktop widgets and the dock.")
            values: ["compact", "normal", "comfortable"]
            labels: [qsTr("Compact"), qsTr("Normal"), qsTr("Comfortable")]
            value: page.look.density || "normal"
            commit: v => page.settings.setDesktop("density", v)
        }
        SettingCombo {
            objectName: "desktopQualityCombo"
            entry: "desktop.quality"
            title: qsTr("Effects quality on the desktop")
            values: ["low", "balanced", "cinematic"]
            labels: [qsTr("Low — fewest effects, least GPU"), qsTr("Balanced"), qsTr("Cinematic — full detail")]
            value: page.look.quality || "balanced"
            commit: v => page.settings.setDesktop("quality", v)
        }
        SettingRow {
            entry: "desktop.core"
            title: qsTr("Core size and place")
            hint: qsTr("The Jarvis core on the desktop.")
            Row {
                spacing: 8
                ComboBox {
                    width: 130
                    readonly property var values: ["small", "medium", "large"]
                    model: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
                    currentIndex: Math.max(0, values.indexOf(page.look.coreSize || "medium"))
                    Accessible.name: qsTr("Core size")
                    onActivated: index => {
                        page.settings.setDesktop("coreSize", values[index]); page.shell.update()
                        currentIndex = Qt.binding(() => Math.max(0, values.indexOf(page.look.coreSize || "medium")))
                    }
                }
                ComboBox {
                    width: 130
                    readonly property var values: ["center", "left", "right"]
                    model: [qsTr("Centre"), qsTr("Left"), qsTr("Right")]
                    currentIndex: Math.max(0, values.indexOf(page.look.corePosition || "center"))
                    Accessible.name: qsTr("Core place")
                    onActivated: index => {
                        page.settings.setDesktop("corePosition", values[index]); page.shell.update()
                        currentIndex = Qt.binding(() => Math.max(0, values.indexOf(page.look.corePosition || "center")))
                    }
                }
            }
        }

        // ---- The Jarvis window ----
        SettingGroup { title: qsTr("Jarvis window") }
        SettingCombo {
            objectName: "hudCombo"
            entry: "window.hud"
            title: qsTr("Look of the Jarvis window")
            hint: qsTr("The animated HUD uses the graphics card; the classic chat is the lightest.")
            values: ["cinematic", "balanced", "low", "off"]
            labels: [qsTr("Cinematic HUD: every effect"), qsTr("Balanced HUD"), qsTr("Light HUD: no particles, fewer layers"), qsTr("Classic chat without the HUD")]
            value: page.settings.hudQuality
            comboWidth: 320
            commit: v => { page.settings.hudQuality = v; return "" }
        }

        // ---- Dock ----
        SettingGroup {
            title: qsTr("Dock")
            text: page.dockHere ? qsTr("The same values as in the dock's own settings (right click on the dock): a change there shows here, and the other way round.")
                                : (page.dock.reading ? qsTr("Reading the dock from Plasma…")
                                   : page.dock.error ? page.dock.error
                                   : qsTr("The Jarvis dock is not on a panel. Position, height, alignment and hiding are saved and used when JARVIS DESKTOP adds the dock."))
        }
        SettingCombo {
            entry: "dock.position"
            title: qsTr("Dock position")
            values: ["bottom", "top", "left", "right"]
            labels: [qsTr("Bottom"), qsTr("Top"), qsTr("Left"), qsTr("Right")]
            value: page.panelValue("dockPosition", "bottom")
            commit: v => page.shell.setDock("dockPosition", v)
        }
        SettingRow {
            id: dockSizeRow
            entry: "dock.size"
            title: qsTr("Dock height")
            hint: qsTr("From 32 to 72 pixels.")
            Row {
                spacing: 8
                SpinBox {
                    id: dockSpin
                    objectName: "dockSizeSpin"
                    from: 32; to: 72
                    editable: true
                    value: page.panelValue("dockSize", 44)
                    Accessible.name: dockSizeRow.title
                    onValueModified: {
                        dockSizeRow.error = page.shell.setDock("dockSize", value)
                        value = Qt.binding(() => page.panelValue("dockSize", 44))
                    }
                }
                Label { anchors.verticalCenter: parent.verticalCenter; text: qsTr("px"); color: Theme.textDim }
            }
        }
        SettingCombo {
            entry: "dock.alignment"
            title: qsTr("Dock alignment")
            values: ["center", "left", "right"]
            labels: [qsTr("Centred"), qsTr("At the start"), qsTr("At the end")]
            value: page.panelValue("dockAlignment", "center")
            commit: v => page.shell.setDock("dockAlignment", v)
        }
        SettingSwitch {
            entry: "dock.autohide"
            title: qsTr("Show the dock automatically")
            hint: qsTr("The dock stays hidden and slides in when the pointer touches the screen edge.")
            checked: !!page.panelValue("dockAutohide", false)
            commit: on => page.shell.setDock("dockAutohide", on)
        }
        SettingSlider {
            entry: "dock.icons"
            enabled: page.dockHere
            title: qsTr("Icon size in the dock")
            hint: qsTr("How much of the dock's height an icon takes.")
            from: 40; to: 85; stepSize: 1
            value: page.dock.iconSize || 62
            valueText: v => Math.round(v) + " %"
            commit: v => page.shell.setDock("iconSize", Math.round(v))
        }
        SettingSwitch {
            entry: "dock.magnify"
            enabled: page.dockHere
            title: qsTr("Magnify icons under the pointer")
            checked: page.dock.magnification !== false
            commit: on => page.shell.setDock("magnification", on)
        }
        SettingSlider {
            enabled: page.dockHere && page.dock.magnification !== false
            title: qsTr("How much larger")
            from: 5; to: 60; stepSize: 1
            value: page.dock.magnifyAmount || 35
            valueText: v => "+" + Math.round(v) + " %"
            commit: v => page.shell.setDock("magnifyAmount", Math.round(v))
        }
        SettingSwitch {
            entry: "dock.glass"
            enabled: page.dockHere
            title: qsTr("Jarvis glass under the whole panel")
            hint: qsTr("The dock, the system tray and the clock on one dark glass. Off: Plasma's usual panel background.")
            checked: page.dock.panelStyle !== false
            commit: on => page.shell.setDock("panelStyle", on)
        }
        SettingSwitch {
            entry: "dock.parts"
            enabled: page.dockHere
            title: qsTr("Workspace buttons in the dock")
            hint: qsTr("GENERAL, STUDY, WORK, DEV; they appear while the Jarvis service runs.")
            checked: page.dock.showProfiles !== false
            commit: on => page.shell.setDock("showProfiles", on)
        }
        SettingSwitch {
            enabled: page.dockHere
            title: qsTr("Virtual desktops in the dock")
            checked: page.dock.showDesktops !== false
            commit: on => page.shell.setDock("showDesktops", on)
        }
        SettingSwitch {
            enabled: page.dockHere
            title: qsTr("Jarvis button in the dock")
            checked: page.dock.showJarvis !== false
            commit: on => page.shell.setDock("showJarvis", on)
        }
        SettingSwitch {
            enabled: page.dockHere
            title: qsTr("One button per application")
            hint: qsTr("Off: one button per window.")
            checked: page.dock.groupWindows !== false
            commit: on => page.shell.setDock("groupWindows", on)
        }

        // ---- Panels and widgets ----
        SettingGroup { title: qsTr("Panels and widgets") }
        SettingSwitch {
            entry: "desktop.replacePanels"
            title: qsTr("Replace the standard panels with the dock")
            hint: qsTr("Used when JARVIS DESKTOP is turned on. Off: the dock is added and your panels stay. On: they are removed after the dock is verified to work, and come back with a restore.")
            checked: page.look.replacePanels !== false
            commit: on => page.settings.setDesktop("replacePanels", on)
        }
        SettingSwitch {
            entry: "desktop.effects"
            title: qsTr("Animations in the desktop widgets")
            hint: qsTr("Off: the core and the widgets stand still and only update their numbers.")
            checked: page.look.effects !== false
            commit: on => page.settings.setDesktop("effects", on)
        }
        SettingRow {
            entry: "widgets.add"
            title: qsTr("Desktop widgets")
            hint: page.plasma.plasmaRunning ? qsTr("Add a widget, or remove all widgets of that kind. Move and resize them in Plasma's edit mode.")
                                            : qsTr("Plasma is not running here.")
            ColumnLayout {
                spacing: 6
                Flow {
                    Layout.preferredWidth: SettingsState.narrow ? page.width - 60 : 330
                    spacing: 6
                    Repeater {
                        model: [["org.jarvis.system", qsTr("System")], ["org.jarvis.network", qsTr("Network")],
                                ["org.jarvis.power", qsTr("Power")], ["org.jarvis.media", qsTr("Media")], ["org.jarvis.status", qsTr("Status")]]
                        Row {
                            required property var modelData
                            spacing: 2
                            DarkButton { text: "+ " + modelData[1]; enabled: page.plasma.plasmaRunning && !page.plasma.busy; Accessible.name: qsTr("Add %1").arg(modelData[1]); onClicked: page.plasma.addWidget(modelData[0]) }
                            DarkButton { text: "−"; enabled: page.plasma.plasmaRunning && !page.plasma.busy; Accessible.name: qsTr("Remove %1").arg(modelData[1]); onClicked: page.plasma.removeWidgets(modelData[0]) }
                        }
                    }
                }
                Row {
                    spacing: 6
                    DarkButton { text: qsTr("Edit the layout in Plasma"); onClicked: page.shell.setEditMode(true) }
                    DarkButton { text: qsTr("Done editing"); onClicked: page.shell.setEditMode(false) }
                }
            }
        }
        SettingRow {
            entry: "widgets.hud"
            title: qsTr("Jarvis HUD widget")
            hint: !page.plasma.widgetInstalled ? qsTr("The widget package is not installed.")
                : !page.plasma.plasmaRunning ? qsTr("Plasma is not running here.")
                : page.plasma.widgetCount < 0 ? qsTr("Checking the desktop…")
                : page.plasma.widgetCount === 0 ? qsTr("Not on the desktop.")
                : qsTr("On the desktop: %1").arg(page.plasma.widgetCount)
            Row {
                spacing: 6
                DarkButton {
                    objectName: "addWidgetButton"
                    text: qsTr("Add…")
                    primary: true
                    enabled: page.plasma.widgetInstalled && page.plasma.plasmaRunning && !page.plasma.busy && page.plasma.widgetCount === 0
                    onClicked: addWidgetConsent.open()
                }
                DarkButton {
                    text: qsTr("Remove…")
                    enabled: page.plasma.plasmaRunning && !page.plasma.busy && page.plasma.widgetCount > 0
                    onClicked: removeWidgetConsent.open()
                }
            }
        }
        SettingCombo {
            objectName: "hudAnimationCombo"
            entry: "widgets.animation"
            title: qsTr("HUD widget animations")
            hint: qsTr("On battery the widget uses one step less; under maximized or fullscreen windows it stops.")
            values: ["off", "low", "normal", "high"]
            labels: [qsTr("Off (still)"), qsTr("Low"), qsTr("Normal"), qsTr("High")]
            value: page.settings.hudAnimation
            commit: v => page.settings.setHudAnimation(v)
        }
        Label { visible: page.plasma.status.length > 0; Layout.leftMargin: 10; text: page.plasma.status; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }

        // ---- Workspaces ----
        SettingGroup {
            title: qsTr("Workspaces")
            text: qsTr("Each workspace has a KDE virtual desktop, the applications it starts, the files \"prepare\" opens, the widgets it shows and how active Jarvis is there. Switch here, in the dock, in KRunner (\"switch to dev\") or by asking. Switching never closes an application.")
        }
        Item {
            objectName: "entry:workspaces.profiles"
            Layout.fillWidth: true
            implicitHeight: 1
        }
        Repeater {
            model: page.shell.profiles
            Rectangle {
                id: profileCard
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                implicitHeight: profileColumn.implicitHeight + 20
                radius: 10
                color: Theme.panel
                border.color: Theme.border
                ColumnLayout {
                    id: profileColumn
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: profileCard.modelData.label; color: Theme.text; font.bold: true; font.letterSpacing: 2; height: 38; verticalAlignment: Text.AlignVCenter }
                        Label { text: qsTr("desktop"); color: Theme.textDim; height: 38; verticalAlignment: Text.AlignVCenter }
                        SpinBox {
                            from: 1; to: 20
                            value: profileCard.modelData.desktop
                            Accessible.name: qsTr("Virtual desktop of %1").arg(profileCard.modelData.label)
                            onValueModified: page.shell.setProfile(profileCard.modelData.id, "desktop", value)
                        }
                        DarkButton { text: qsTr("Switch"); enabled: page.win.daemonUp; onClicked: page.win.desktop.switchWorkspace(profileCard.modelData.id) }
                        DarkButton { text: qsTr("Prepare"); enabled: page.win.daemonUp; onClicked: page.win.desktop.switchWorkspace("prepare:" + profileCard.modelData.id) }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: profileCard.modelData.apps || []
                            Rectangle {
                                id: appChip
                                required property int index
                                required property string modelData
                                readonly property var info: page.shell.describeApp(modelData)
                                height: 30; radius: 15
                                width: chipRow.implicitWidth + 16
                                color: Theme.bg
                                border.color: info.installed ? Theme.border : Theme.danger
                                Row {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Label { anchors.verticalCenter: parent.verticalCenter; text: appChip.info.name + (appChip.info.installed ? "" : " · " + qsTr("not installed")); color: Theme.text }
                                    ToolButton {
                                        width: 24; height: 24
                                        text: "×"
                                        Accessible.name: qsTr("Remove %1").arg(appChip.info.name)
                                        onClicked: {
                                            const list = (profileCard.modelData.apps || []).filter((x, i) => i !== appChip.index)
                                            page.shell.setProfile(profileCard.modelData.id, "apps", list)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    TextField {
                        id: appSearch
                        Layout.fillWidth: true
                        placeholderText: qsTr("Add an application: type its name")
                        property var found: text.trim().length >= 2 ? page.shell.findApps(text) : []
                        onAccepted: if (found.length > 0) addFound(found[0].id)
                        function addFound(id) {
                            const list = (profileCard.modelData.apps || []).filter(x => x !== id)
                            list.push(id)
                            page.shell.setProfile(profileCard.modelData.id, "apps", list)
                            text = ""
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: appSearch.found.length > 0
                        Repeater {
                            model: appSearch.found
                            DarkButton { required property var modelData; text: "+ " + modelData.name; onClicked: appSearch.addFound(modelData.id) }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Label { text: qsTr("Jarvis here"); color: Theme.textDim; height: 38; verticalAlignment: Text.AlignVCenter }
                        ComboBox {
                            width: Math.min(320, profileColumn.width - 100)
                            readonly property var values: ["normal", "reduced", "quiet"]
                            model: [qsTr("As in Actions and permissions"), qsTr("Fewer suggestions (at most assistive)"), qsTr("Quiet: no suggestions, answers only")]
                            currentIndex: Math.max(0, values.indexOf(profileCard.modelData.activity || "normal"))
                            Accessible.name: qsTr("Jarvis here")
                            onActivated: index => page.shell.setProfile(profileCard.modelData.id, "activity", values[index])
                        }
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: (profileCard.modelData.paths || []).join(", ")
                        placeholderText: qsTr("Files, folders or projects that \"prepare\" opens")
                        onEditingFinished: page.shell.setProfile(profileCard.modelData.id, "paths", text.split(",").map(x => x.trim()).filter(x => x.length > 0))
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: [["org.jarvis.system", qsTr("System")], ["org.jarvis.network", qsTr("Network")], ["org.jarvis.power", qsTr("Power")],
                                    ["org.jarvis.media", qsTr("Media")], ["org.jarvis.status", qsTr("Status")]]
                            CheckBox {
                                required property var modelData
                                text: modelData[1]
                                checked: (profileCard.modelData.widgets || []).indexOf(modelData[0]) >= 0
                                onToggled: {
                                    let list = (profileCard.modelData.widgets || []).filter(w => w !== modelData[0])
                                    if (checked) list.push(modelData[0])
                                    page.shell.setProfile(profileCard.modelData.id, "widgets", list)
                                }
                            }
                        }
                    }
                }
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            spacing: 8
            DarkButton { text: qsTr("Save the window layout of this workspace"); enabled: page.win.daemonUp; onClicked: page.win.desktop.saveWorkspaceLayout() }
            DarkButton { text: qsTr("Create the workspaces' desktops…"); onClicked: desktopsConsent.open() }
        }
    }

    ColorDialog {
        id: colourDialog
        property string key: "primary"
        title: qsTr("Desktop colour")
        onAccepted: page.settings.setDesktop(key, String(selectedColor).substring(0, 7))
    }
    ConfirmDialog {
        id: applyConsent
        width: Math.min(520, page.win.width - 40)
        title: qsTr("Turn on JARVIS DESKTOP?")
        acceptText: qsTr("Turn on")
        ColumnLayout {
            width: parent.width
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Jarvis will make these changes to your Plasma desktop:") }
            Repeater {
                model: page.shell.plan
                Label { required property string modelData; Layout.fillWidth: true; wrapMode: Text.Wrap; text: "• " + modelData; color: Theme.textDim; textFormat: Text.PlainText }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("If anything does not work, the changes are undone at once. You can always go back with \"Back to standard KDE\" or jarvis --restore-desktop.") }
        }
        onAccepted: page.shell.apply()
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
    ConfirmDialog {
        id: desktopsConsent
        width: Math.min(480, page.win.width - 40)
        title: qsTr("Create virtual desktops for the workspaces?")
        acceptText: qsTr("Create")
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("KWin gets as many virtual desktops as the workspaces need (named GENERAL, STUDY, WORK, DEV). Existing desktops are not renamed or removed.") }
        onAccepted: page.shell.createWorkspaceDesktops()
    }
    ConfirmDialog {
        id: addWidgetConsent
        width: Math.min(480, page.win.width - 40)
        title: qsTr("Add the Jarvis widget to your desktop?")
        acceptText: qsTr("Add")
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("Jarvis will ask Plasma to place one \"Jarvis HUD\" widget at the top right of the first screen's desktop. Nothing else in your layout changes. You can move it, resize it or remove it like any widget, or here.") }
        onAccepted: page.plasma.addWidget()
    }
    ConfirmDialog {
        id: removeWidgetConsent
        width: Math.min(480, page.win.width - 40)
        title: qsTr("Remove the Jarvis widgets?")
        acceptText: qsTr("Remove")
        destructive: true
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("Every \"Jarvis HUD\" widget leaves your desktops and panels. Other widgets are not touched.") }
        onAccepted: page.plasma.removeWidgets()
    }
}
