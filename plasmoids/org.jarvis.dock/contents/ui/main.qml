import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import org.kde.plasma.workspace.dbus as DBus
import org.jarvis.visual as JV

// Jarvis Dock (0.19.3): a task bar for a Plasma panel in the Jarvis look, on
// Plasma's own task model — the same windows, launchers, grouping and
// requests as the standard task manager, on Wayland and X11. Jarvis's parts
// (the core button, the workspaces) talk to jarvisd; everything else works
// without it, so a stopped Jarvis never takes the task bar away. The system
// tray and the clock beside it are Plasma's own widgets (the desktop shell
// puts them in the same panel).
//
// The launcher ("Start") is a popup of the Plasma applet-popup kind — the
// one Kickoff uses — opened by its button, by the Meta key (the dock
// provides Plasma's launcher-menu role) or by the applet's shortcut.
PlasmoidItem {
    id: root
    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
    readonly property int thickness: Math.max(24, vertical ? width : height)
    readonly property int cell: thickness - 2 * padding
    readonly property int padding: Math.max(2, Math.round(Plasmoid.configuration.padding))
    readonly property bool effects: Plasmoid.configuration.effects && link.desktop.effects !== false
                                    && !(link.status.animation && link.status.animation.effective === "off")
    readonly property real glow: Plasmoid.configuration.glow / 100
    readonly property bool magnify: effects && Plasmoid.configuration.magnification
    readonly property real maxZoom: Math.min(0.95 / (Plasmoid.configuration.iconSize / 100), 1 + Plasmoid.configuration.magnifyAmount / 100)
    // The screen edge the panel sits on: indicators and growth face it.
    readonly property int edge: Plasmoid.location === PlasmaCore.Types.TopEdge ? Qt.TopEdge
                              : Plasmoid.location === PlasmaCore.Types.LeftEdge ? Qt.LeftEdge
                              : Plasmoid.location === PlasmaCore.Types.RightEdge ? Qt.RightEdge : Qt.BottomEdge

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.constraintHints: Plasmoid.CanFillArea
    preferredRepresentation: fullRepresentation
    activationTogglesExpanded: false
    // The Meta key and the applet's own shortcut: the launcher.
    Connections {
        target: Plasmoid
        function onActivated() { root.toggleLauncher() }
    }

    JV.DaemonLink {
        id: link
        component: "dock"
        onEvent: e => { if (e.kind === "notice" || e.kind === "error") jarvisButton.flash() }
    }
    TaskManager.VirtualDesktopInfo { id: virtualDesktops }
    TaskManager.ActivityInfo { id: activityInfo }
    TaskManager.TasksModel {
        id: tasks
        sortMode: TaskManager.TasksModel.SortManual
        groupMode: Plasmoid.configuration.groupWindows ? TaskManager.TasksModel.GroupApplications : TaskManager.TasksModel.GroupDisabled
        groupInline: false
        separateLaunchers: false
        launchInPlace: true
        filterByVirtualDesktop: true
        filterByActivity: true
        filterByScreen: Plasmoid.configuration.filterByScreen
        screenGeometry: Plasmoid.containment ? Plasmoid.containment.screenGeometry : Qt.rect(0, 0, 0, 0)
        virtualDesktop: virtualDesktops.currentDesktop
        activity: activityInfo.currentActivity
        launcherList: root.cleanLaunchers(Plasmoid.configuration.launchers)
        onLauncherListChanged: {
            if (JSON.stringify(launcherList) !== JSON.stringify(Plasmoid.configuration.launchers))
                Plasmoid.configuration.launchers = launcherList
        }
    }
    // Pinned entries that cannot be shown: an executable instead of a .desktop file
    // (an old snap launcher: a blank icon that starts nothing) is dropped.
    function cleanLaunchers(list) {
        return (list || []).filter(u => u.startsWith("applications:") || u.startsWith("preferred:")
                                        || (u.startsWith("file:") && u.endsWith(".desktop")))
    }
    function modelIndex(i) { return tasks.makeModelIndex(i) }
    function switchDesktop(number) {
        DBus.SessionBus.asyncCall({ service: "org.kde.KWin", path: "/KWin", interface: "org.kde.KWin", member: "setCurrentDesktop",
                                    arguments: [number], signature: "(i)" }, () => {}, reply => console.warn("Jarvis dock: desktop switch:", reply.error.message))
    }
    function pin(url) { if (url && tasks.launcherPosition(url) < 0) tasks.requestAddLauncher(url) }
    function unpin(url) { if (url) tasks.requestRemoveLauncher(url) }

    // ------------------------------------------------------ the whole panel
    // 0.20: the system tray and the clock beside the dock are Plasma's own widgets,
    // and under them Plasma drew its own panel background (light in a light theme):
    // the dock's glass ended where they began, whatever the dock's look. Now one
    // Jarvis glass lies over Plasma's panel background and under all of the panel's
    // widgets, and the widgets there take the complementary colours (light on dark) —
    // Plasma's tray, clock and popups themselves are unchanged. Plasma's background
    // stays underneath on purpose: the panel's input region follows it, and without
    // it (NoBackground) a panel that grew kept its old input region — the tray and
    // the clock past the old end ignored clicks. Off in the settings, or with the
    // dock gone, the panel is Plasma's again.
    readonly property bool wantPanelStyle: Plasmoid.configuration.panelStyle && Plasmoid.formFactor !== PlasmaCore.Types.Planar
    property Item panelItem: null
    property Item panelGlass: null
    readonly property bool panelStyled: panelGlass !== null
    Component { id: panelGlassComponent; PanelGlass {} }
    function containmentItem() {
        for (let p = root.parent; p; p = p.parent)
            if (p.plasmoid !== undefined && p.plasmoid === Plasmoid.containment)
                return p
        return null
    }
    function stylePanel() {
        const item = wantPanelStyle ? containmentItem() : null
        if (item === panelItem && (item === null) === (panelGlass === null))
            return
        unstylePanel()
        if (!item)
            return
        panelItem = item
        panelGlass = panelGlassComponent.createObject(item, {
            surfaceOpacity: Qt.binding(() => Plasmoid.configuration.surfaceOpacity / 100),
            glow: Qt.binding(() => root.glow), effects: Qt.binding(() => root.effects),
            edge: Qt.binding(() => root.edge), vertical: Qt.binding(() => root.vertical) })
        panelBefore = { inherit: item.Kirigami.Theme.inherit, colorSet: item.Kirigami.Theme.colorSet }
        item.Kirigami.Theme.inherit = false
        item.Kirigami.Theme.colorSet = Kirigami.Theme.Complementary
    }
    property var panelBefore: null
    function unstylePanel() {
        if (panelGlass) panelGlass.destroy()
        panelGlass = null
        if (panelItem && panelBefore) {
            panelItem.Kirigami.Theme.colorSet = panelBefore.colorSet
            panelItem.Kirigami.Theme.inherit = panelBefore.inherit
        }
        panelItem = null
        panelBefore = null
    }
    onWantPanelStyleChanged: Qt.callLater(stylePanel)
    onParentChanged: Qt.callLater(stylePanel)
    Component.onCompleted: Qt.callLater(stylePanel)
    Component.onDestruction: unstylePanel()

    // ---------------------------------------------------------------- launcher
    // An applet popup (Wayland role "appletpopup"): KWin places it by the button
    // on the panel's screen, above the panel, and gives it the keyboard.
    // Why 0.19.2's launcher "did nothing": it asked for a stays-on-top popup menu,
    // which libplasma turns into a second *panel* surface, with
    // hideOnWindowDeactivate — and a click on a panel gives the panel the focus,
    // which libplasma reads as "a Plasma view was clicked": the popup hid itself
    // the moment it appeared. Here, as Plasma's own task manager does, the popup
    // asks for the focus once it is shown and closes only when it has stayed
    // without it for a moment.
    component JarvisPopup: PlasmaCore.Dialog {
        id: popup
        location: Plasmoid.location
        type: PlasmaCore.Dialog.AppletPopup
        hideOnWindowDeactivate: false
        backgroundHints: PlasmaCore.Dialog.NoBackground
        property double hiddenAt: 0
        signal opened()
        onVisibleChanged: {
            if (visible) {
                Plasmoid.status = PlasmaCore.Types.RequiresAttentionStatus  // an auto-hiding panel stays out
                popup.requestActivate()
                Qt.callLater(popup.requestActivate)
                popup.opened()
            } else {
                hiddenAt = Date.now()
                if (!launcherDialog.visible && !windowsDialog.visible)
                    Plasmoid.status = PlasmaCore.Types.ActiveStatus
            }
        }
        onActiveChanged: if (!active && visible) closer.restart()
        property Timer closer: Timer {
            interval: 160
            onTriggered: if (!popup.active) popup.visible = false
        }
        // The press that closed it (the focus went elsewhere) must not reopen it on release.
        function toggle(anchor) {
            if (popup.visible) {
                popup.visible = false
            } else if (Date.now() - popup.hiddenAt > 300) {
                if (anchor) popup.visualParent = anchor
                popup.visible = true
            }
        }
    }
    JarvisPopup {
        id: launcherDialog
        visualParent: root.launcherAnchor
        mainItem: LauncherView {
            id: launcherView
            appletInterface: root
            pinned: tasks.launcherList
            jarvisOnline: link.online
            effects: root.effects
            onDone: launcherDialog.visible = false
            onPinRequested: url => root.pin(url)
            onUnpinRequested: url => root.unpin(url)
            onJarvisRequested: link.online ? link.run(link.integrated ? "overlay" : "main") : link.startJarvis()
        }
        onOpened: launcherView.reset()
    }
    property Item launcherAnchor: null   // the Start button (inside the representation)
    function toggleLauncher() { launcherDialog.toggle(root.launcherAnchor) }

    // ------------------------------------------------- the windows of a group
    JarvisPopup {
        id: windowsDialog
        property int row: -1
        mainItem: WindowList {
            id: windowList
            tasksModel: tasks
            row: windowsDialog.row
            onDone: windowsDialog.visible = false
        }
        onOpened: windowList.forceActiveFocus()
    }
    function showWindows(row, item) {
        if (windowsDialog.visible && windowsDialog.row === row) {
            windowsDialog.visible = false
            return
        }
        windowsDialog.row = row
        windowsDialog.toggle(item)
    }

    // ------------------------------------------------------- the context menu
    // A real menu window (PlasmaExtras.Menu, as Plasma's own task manager): a
    // Qt Quick menu inside the panel would be cut to the panel's height.
    property var menuInfo: ({})
    PlasmaExtras.Menu {
        id: taskMenu
        visualParent: null
        placement: root.edge === Qt.TopEdge ? PlasmaExtras.Menu.BottomPosedLeftAlignedPopup
                 : root.edge === Qt.LeftEdge ? PlasmaExtras.Menu.RightPosedTopAlignedPopup
                 : root.edge === Qt.RightEdge ? PlasmaExtras.Menu.LeftPosedTopAlignedPopup
                 : PlasmaExtras.Menu.TopPosedLeftAlignedPopup
    }
    Component { id: menuItemComponent; PlasmaExtras.MenuItem {} }
    function addMenuItem(props, action) {
        const item = menuItemComponent.createObject(taskMenu, props)
        if (action) item.clicked.connect(action)
        taskMenu.addMenuItem(item)
    }
    function openTaskMenu(row, item) {
        const idx = modelIndex(row)
        const url = tasks.data(idx, TaskManager.AbstractTasksModel.LauncherUrlWithoutIcon) || ""
        const isWindow = tasks.data(idx, TaskManager.AbstractTasksModel.IsWindow) === true
        const isGroup = tasks.data(idx, TaskManager.AbstractTasksModel.IsGroupParent) === true
        const count = isGroup ? tasks.rowCount(idx) : (isWindow ? 1 : 0)
        const pinned = !!url && tasks.launcherPosition(url) >= 0
        taskMenu.clearMenuItems()
        addMenuItem({ section: true, text: tasks.data(idx, Qt.DisplayRole) || "" })
        addMenuItem({ text: i18nd("jarvis-desktop", "New window"), icon: "window-new" }, () => tasks.requestNewInstance(modelIndex(row)))
        addMenuItem({ text: pinned ? i18nd("jarvis-desktop", "Unpin from the dock") : i18nd("jarvis-desktop", "Pin to the dock"),
                      icon: pinned ? "window-unpin" : "window-pin", enabled: !!url },
                    () => pinned ? root.unpin(url) : root.pin(url))
        if (count > 0) {
            addMenuItem({ separator: true })
            if (count > 1)
                addMenuItem({ text: i18nd("jarvis-desktop", "Choose a window (%1)…", count), icon: "view-list-details" }, () => root.showWindows(row, item))
            const minimized = tasks.data(idx, TaskManager.AbstractTasksModel.IsMinimized) === true
            if (isWindow) {
                addMenuItem({ text: minimized ? i18nd("jarvis-desktop", "Restore") : i18nd("jarvis-desktop", "Minimize"), icon: "window-minimize" },
                            () => tasks.requestToggleMinimized(modelIndex(row)))
                addMenuItem({ text: i18nd("jarvis-desktop", "Maximize or restore"), icon: "window-maximize" }, () => tasks.requestToggleMaximized(modelIndex(row)))
                addMenuItem({ text: i18nd("jarvis-desktop", "Keep above others"), icon: "window-keep-above", checkable: true,
                              checked: tasks.data(idx, TaskManager.AbstractTasksModel.IsKeepAbove) === true },
                            () => tasks.requestToggleKeepAbove(modelIndex(row)))
            }
            // Another virtual desktop.
            if (virtualDesktops.numberOfDesktops > 1) {
                for (let d = 0; d < virtualDesktops.numberOfDesktops; ++d) {
                    const id = virtualDesktops.desktopIds[d]
                    if (id === virtualDesktops.currentDesktop) continue
                    addMenuItem({ text: i18nd("jarvis-desktop", "Move to desktop %1", virtualDesktops.desktopNames[d] || String(d + 1)), icon: "virtual-desktops" },
                                () => tasks.requestVirtualDesktops(modelIndex(row), [id]))
                }
            }
            addMenuItem({ separator: true })
            addMenuItem({ text: count > 1 ? i18nd("jarvis-desktop", "Close all %1 windows", count) : i18nd("jarvis-desktop", "Close"), icon: "window-close" },
                        () => tasks.requestClose(modelIndex(row)))
        }
        taskMenu.visualParent = item
        taskMenu.openRelative()
    }

    // Reordering: the dragged task follows the pointer; the pinned order is saved when it is dropped.
    property int dragRow: -1
    function dragTo(row, scenePoint) {
        const p = taskList.mapFromItem(null, scenePoint.x, scenePoint.y)
        const target = taskList.indexAt(p.x + taskList.contentX, p.y + taskList.contentY)
        if (target >= 0 && target !== row && tasks.move(row, target))
            dragRow = target
    }

    fullRepresentation: Item {
        id: dock
        // The fixed part (Jarvis, Start, the workspaces and desktops) and the applications.
        // 0.20: the dock used to insist on its whole length, so with many applications, a small
        // screen or a vertical panel the panel ran past the screen's edge and Plasma's tray and
        // clock after the dock were cut off or gone. Now it asks for its whole length but takes
        // less when the panel has no more room: the applications then scroll (wheel, or the
        // active one is brought into view), and the tray and clock stay on the screen.
        readonly property real gap: row.spacing
        readonly property real frame: 2 * root.padding + 12
        readonly property real headLength: root.vertical ? head.implicitHeight : head.implicitWidth
        readonly property real tasksLength: root.vertical ? taskList.contentHeight : taskList.contentWidth
        readonly property real fullLength: Math.ceil(headLength + gap + tasksLength + frame)
        readonly property real shortLength: Math.ceil(headLength + gap + Math.min(tasksLength, 2 * root.cell + gap) + frame)
        readonly property real tasksRoom: Math.max(0, (root.vertical ? height : width) - headLength - gap - frame)
        Layout.minimumWidth: root.vertical ? -1 : shortLength
        Layout.preferredWidth: root.vertical ? -1 : fullLength
        Layout.maximumWidth: root.vertical ? -1 : fullLength
        Layout.minimumHeight: root.vertical ? shortLength : -1
        Layout.preferredHeight: root.vertical ? fullLength : -1
        Layout.maximumHeight: root.vertical ? fullLength : -1
        Layout.fillWidth: false
        Layout.fillHeight: false

        // On a Jarvis panel the panel's glass is the surface; the dock ends with a line before Plasma's widgets.
        Rectangle {
            visible: root.panelStyled
            x: root.vertical ? (parent.width - width) / 2 : parent.width - 1
            y: root.vertical ? parent.height - 1 : (parent.height - height) / 2
            width: root.vertical ? parent.width * 0.6 : 1
            height: root.vertical ? 1 : parent.height * 0.6
            color: JV.JarvisTheme.edge
        }
        // Otherwise the dock's own surface: dark glass, a thin holographic edge, corner ticks, a light
        // outline that keeps it visible on a light wallpaper. It unfolds when the dock starts.
        Rectangle {
            id: surface
            visible: !root.panelStyled
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.min(JV.JarvisTheme.radius + 2, height / 2)
            color: JV.JarvisTheme.alpha(JV.JarvisTheme.surface, Plasmoid.configuration.surfaceOpacity / 100)
            border.color: JV.JarvisTheme.alpha(JV.JarvisTheme.edge, Math.min(1, JV.JarvisTheme.edge.a * Math.max(0.3, root.glow)))
            transformOrigin: root.edge === Qt.TopEdge ? Item.Top : root.edge === Qt.LeftEdge ? Item.Left : root.edge === Qt.RightEdge ? Item.Right : Item.Bottom
            opacity: 0
            scale: 0.96
            Component.onCompleted: { opacity = 1; scale = 1 }
            Behavior on opacity { enabled: root.effects; NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
            Behavior on scale { enabled: root.effects; NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
            Rectangle {
                anchors.fill: parent; anchors.margins: -1
                radius: parent.radius + 1
                color: "transparent"; border.color: JV.JarvisTheme.outline; z: -1
            }
            // The glow along the screen-side edge.
            Rectangle {
                id: edgeGlow
                visible: root.glow > 0
                readonly property bool horizontal: root.edge === Qt.BottomEdge || root.edge === Qt.TopEdge
                width: horizontal ? parent.width * 0.6 : 1
                height: horizontal ? 1 : parent.height * 0.6
                x: horizontal ? (parent.width - width) / 2 : root.edge === Qt.LeftEdge ? 0 : parent.width - 1
                y: horizontal ? (root.edge === Qt.BottomEdge ? parent.height - 1 : 0) : (parent.height - height) / 2
                gradient: Gradient {
                    orientation: edgeGlow.horizontal ? Gradient.Horizontal : Gradient.Vertical
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 0.5; color: JV.JarvisTheme.alpha(JV.JarvisTheme.cyan, Math.min(1, 0.7 * root.glow)) }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Repeater {
                model: root.glow > 0 ? 4 : 0
                Item {
                    required property int index
                    width: 9; height: 9
                    x: index % 2 ? surface.width - width - 5 : 5
                    y: index < 2 ? 5 : surface.height - height - 5
                    opacity: Math.min(1, 0.8 * root.glow)
                    Rectangle { width: 9; height: 2; color: JV.JarvisTheme.cyan; y: parent.index < 2 ? 0 : 7 }
                    Rectangle { width: 2; height: 9; color: JV.JarvisTheme.cyan; x: parent.index % 2 ? 7 : 0 }
                }
            }
        }

        // Not enough room for every application: the wheel anywhere on the dock scrolls them.
        // (On the list itself the handler would sit in the ListView's content item and never see the wheel.)
        WheelHandler {
            enabled: taskList.overflowing
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad   // a touchpad's two-finger scroll too
            onWheel: event => {
                const delta = (event.angleDelta.y || event.angleDelta.x) / 120 * root.cell
                if (root.vertical) taskList.contentY = Math.max(0, Math.min(taskList.contentHeight - taskList.height, taskList.contentY - delta))
                else taskList.contentX = Math.max(0, Math.min(taskList.contentWidth - taskList.width, taskList.contentX - delta))
            }
        }
        // More applications past an end: that end fades out.
        Repeater {
            model: taskList.overflowing ? 2 : 0
            Rectangle {
                required property int index
                readonly property real scrolled: root.vertical ? taskList.contentY - taskList.originY : taskList.contentX - taskList.originX
                readonly property real rest: (root.vertical ? taskList.contentHeight - taskList.height : taskList.contentWidth - taskList.width) - scrolled
                visible: index === 0 ? scrolled > 1 : rest > 1
                z: 2
                readonly property real depth: Math.round(root.cell * 0.6)
                x: row.x + taskList.x + (root.vertical ? 0 : index === 0 ? 0 : taskList.width - width)
                y: row.y + taskList.y + (root.vertical ? (index === 0 ? 0 : taskList.height - height) : 0)
                width: root.vertical ? taskList.width : depth
                height: root.vertical ? depth : taskList.height
                gradient: Gradient {
                    orientation: root.vertical ? Gradient.Vertical : Gradient.Horizontal
                    GradientStop { position: 0; color: index === 0 ? JV.JarvisTheme.surface : "transparent" }
                    GradientStop { position: 1; color: index === 0 ? "transparent" : JV.JarvisTheme.surface }
                }
            }
        }
        // Files dropped on the dock: .desktop files are pinned.
        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: drop => {
                for (const u of drop.urls) {
                    const s = String(u)
                    if (s.endsWith(".desktop")) root.pin(s.includes("/applications/") ? "applications:" + s.substring(s.lastIndexOf("/") + 1) : s)
                }
            }
        }

        Grid {
            id: row
            anchors.centerIn: parent
            // One line: a column on a vertical panel (only `columns` changes, so no frame has rows × columns too small).
            columns: root.vertical ? 1 : 1000
            spacing: Math.round(Plasmoid.configuration.spacing * JV.JarvisTheme.spacing)
            verticalItemAlignment: Grid.AlignVCenter
            horizontalItemAlignment: Grid.AlignHCenter

            Grid {
                id: head
                // One line: a column on a vertical panel (only `columns` changes, so no frame has rows × columns too small).
                columns: root.vertical ? 1 : 1000
                spacing: row.spacing
                verticalItemAlignment: Grid.AlignVCenter
                horizontalItemAlignment: Grid.AlignHCenter
                // Jarvis: the overlay on a click; its state colour; a flash on notifications.
                Item {
                    id: jarvisButton
                    visible: Plasmoid.configuration.showJarvis
                    width: root.cell; height: root.cell
                    function flash() { if (root.effects) flashAnim.restart() }
                    JV.DesktopCore {
                        id: miniCore
                        anchors.fill: parent
                        anchors.margins: 2
                        compact: true
                        status: link.status
                        online: link.online
                        quality: "low"
                        active: root.effects && (jarvisMouse.containsMouse || miniCore.state !== "idle")
                    }
                    Rectangle {
                        id: flashRing
                        anchors.centerIn: parent
                        width: parent.width; height: width; radius: width / 2
                        color: "transparent"; border.color: JV.JarvisTheme.amber; border.width: 2
                        opacity: 0
                        OpacityAnimator { id: flashAnim; target: flashRing; from: 1; to: 0; duration: 900 }
                    }
                    PlasmaCore.ToolTipArea {
                        anchors.fill: parent
                        mainText: "J.A.R.V.I.S."
                        subText: !link.online ? i18nd("jarvis-desktop", "Start Jarvis")
                               : link.integrated ? i18nd("jarvis-desktop", "Click: overlay · right click: the window")
                               : i18nd("jarvis-desktop", "Open the window (the desktop integration is off)")
                    MouseArea {
                        id: jarvisMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: mouse => {
                            if (!link.online) link.startJarvis()
                            else if (mouse.button === Qt.RightButton || !link.integrated) link.run("main")
                            else link.run("overlay")
                        }
                    }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: "Jarvis"
                }
                // Start: the launcher. The whole cell is the button (no gaps to miss).
                Item {
                    id: launcherButton
                    width: root.cell; height: root.cell
                    Component.onCompleted: root.launcherAnchor = launcherButton
                    readonly property bool lit: launcherMouse.containsMouse || launcherDialog.visible
                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.round(root.cell * 0.82); height: width; radius: width / 2
                        color: launcherButton.lit ? JV.JarvisTheme.hoverFill : JV.JarvisTheme.alpha(JV.JarvisTheme.cyan, 0.06)
                        border.color: launcherDialog.visible ? JV.JarvisTheme.edgeStrong : JV.JarvisTheme.edge
                        border.width: launcherDialog.visible ? 1.5 : 1
                        Behavior on color { enabled: root.effects; ColorAnimation { duration: 140 } }
                        // Four ticks: the "start" mark, turning a little when open.
                        Item {
                            anchors.centerIn: parent
                            width: parent.width * 0.5; height: width
                            rotation: launcherDialog.visible ? 45 : 0
                            Behavior on rotation { enabled: root.effects; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                            Repeater {
                                model: 4
                                Rectangle {
                                    required property int index
                                    width: parent.width * 0.38; height: width; radius: 2
                                    x: index % 2 ? parent.width - width : 0
                                    y: index < 2 ? 0 : parent.height - height
                                    color: index === 0 ? JV.JarvisTheme.amber : JV.JarvisTheme.cyan
                                    opacity: launcherButton.lit ? 1 : 0.85
                                }
                            }
                        }
                    }
                    PlasmaCore.ToolTipArea {
                        anchors.fill: parent
                        mainText: i18nd("jarvis-desktop", "Applications")
                        subText: i18nd("jarvis-desktop", "Search, categories, files and the session · Meta")
                        active: !launcherDialog.visible
                        MouseArea {
                            id: launcherMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleLauncher()
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: i18nd("jarvis-desktop", "Applications")
                    Accessible.onPressAction: root.toggleLauncher()
                }
                // Workspaces (from jarvisd) and the virtual desktops (from KWin).
                // A count model: the buttons stay while jarvisd's status changes (an array model
                // rebuilt them on every status — the panel relaid itself out each time).
                Repeater {
                    readonly property var profiles: Plasmoid.configuration.showProfiles && link.integrated && link.status.workspace ? link.status.workspace.profiles || [] : []
                    id: profileButtons
                    model: profiles.length
                    PlasmaCore.ToolTipArea {
                        id: wsTip
                        required property int index
                        readonly property var modelData: profileButtons.profiles[index] || ({ id: "", label: "", desktop: 0 })
                        width: wsButton.width; height: root.cell
                        mainText: i18nd("jarvis-desktop", "Workspace %1", modelData.label)
                        subText: i18nd("jarvis-desktop", "Virtual desktop %1 · its applications and widgets", modelData.desktop)
                        JV.HoloButton {
                            id: wsButton
                            anchors.verticalCenter: parent.verticalCenter
                            width: root.vertical ? root.cell : implicitWidth
                            height: Math.round(root.cell * 0.62)
                            text: root.vertical ? wsTip.modelData.label.charAt(0) : wsTip.modelData.label
                            checked: link.status.workspace.current === wsTip.modelData.id
                            onClicked: link.workspace(wsTip.modelData.id)
                        }
                    }
                }
                Repeater {
                    model: Plasmoid.configuration.showDesktops && virtualDesktops.numberOfDesktops > 1 ? virtualDesktops.desktopIds : []
                    Item {
                        id: desk
                        required property int index
                        required property var modelData
                        readonly property bool current: virtualDesktops.currentDesktop === modelData
                        width: Math.round(root.cell * 0.42); height: root.cell
                        Rectangle {
                            anchors.centerIn: parent
                            width: desk.current ? 11 : 9; height: width; radius: 2
                            rotation: 45
                            color: desk.current ? JV.JarvisTheme.cyan : deskMouse.containsMouse ? JV.JarvisTheme.hoverFill : "transparent"
                            border.color: JV.JarvisTheme.edgeStrong
                        }
                        PlasmaCore.ToolTipArea {
                            anchors.fill: parent
                            mainText: i18nd("jarvis-desktop", "Virtual desktop: %1", virtualDesktops.desktopNames[desk.index] || String(desk.index + 1))
                            MouseArea { id: deskMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.switchDesktop(desk.index + 1) }
                        }
                        Accessible.role: Accessible.Button
                        Accessible.name: virtualDesktops.desktopNames[index] || String(index + 1)
                    }
                }
                Rectangle { width: root.vertical ? root.cell * 0.6 : 1; height: root.vertical ? 1 : root.cell * 0.6; color: JV.JarvisTheme.edge }
            }
            // The applications: pinned launchers and running windows, grouped per application.
            ListView {
                id: taskList
                orientation: root.vertical ? ListView.Vertical : ListView.Horizontal
                interactive: false
                readonly property bool overflowing: (root.vertical ? contentHeight : contentWidth) > dock.tasksRoom + 1
                width: root.vertical ? root.cell : Math.min(contentWidth, dock.tasksRoom)
                height: root.vertical ? Math.min(contentHeight, dock.tasksRoom) : root.cell
                clip: overflowing
                // Not enough room: the active window's application comes into view (the wheel: see the dock).
                Connections {
                    target: tasks
                    function onActiveTaskChanged() {
                        const a = tasks.activeTask
                        if (taskList.overflowing && a.valid) taskList.positionViewAtIndex(a.parent.valid ? a.parent.row : a.row, ListView.Contain)
                    }
                }
                onOverflowingChanged: if (!overflowing) positionViewAtBeginning()
                spacing: Math.round(Plasmoid.configuration.spacing * JV.JarvisTheme.spacing)
                model: tasks
                cacheBuffer: 10000
                // Magnification follows the pointer along the dock.
                readonly property real pointer: hoverTrack.hovered ? (root.vertical ? hoverTrack.point.position.y : hoverTrack.point.position.x) : -1
                HoverHandler { id: hoverTrack }
                add: Transition {
                    enabled: root.effects
                    ParallelAnimation {
                        NumberAnimation { property: "scale"; from: 0.3; to: 1; duration: 320; easing.type: Easing.OutBack }
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 }
                    }
                }
                remove: Transition {
                    enabled: root.effects
                    ParallelAnimation {
                        NumberAnimation { property: "scale"; to: 0.3; duration: 220; easing.type: Easing.InCubic }
                        NumberAnimation { property: "opacity"; to: 0; duration: 220 }
                    }
                }
                displaced: Transition {
                    enabled: root.effects
                    NumberAnimation { properties: "x,y"; duration: 220; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "scale"; to: 1; duration: 120 }
                    NumberAnimation { property: "opacity"; to: 1; duration: 120 }
                }
                delegate: DockTask {
                    id: taskItem
                    required property int index
                    required property var model
                    // Distance from the pointer, in cells: 0 under it.
                    readonly property real center: (root.vertical ? y + height / 2 : x + width / 2) - (root.vertical ? taskList.contentY : taskList.contentX)
                    readonly property real distance: taskList.pointer < 0 ? 99 : Math.abs(center - taskList.pointer) / root.cell
                    size: root.cell
                    iconFraction: Plasmoid.configuration.iconSize / 100
                    zoom: root.magnify ? 1 + (root.maxZoom - 1) * Math.pow(Math.max(0, 1 - distance / 1.6), 2) : (hovered && root.effects ? 1.12 : 1)
                    shift: root.magnify && distance > 0.5 && distance < 2.2 ? Math.sign(center - taskList.pointer) * root.cell * 0.05 * (2.2 - distance) / 1.7 : 0
                    Behavior on shift { enabled: root.effects; NumberAnimation { duration: 90 } }
                    edge: root.edge
                    glow: root.glow
                    name: model.display || ""
                    icon: model.decoration
                    launcher: model.IsLauncher === true
                    active: model.IsActive === true
                    minimized: model.IsMinimized === true
                    attention: model.IsDemandingAttention === true
                    starting: model.IsStartup === true
                    windows: model.IsGroupParent === true ? model.ChildCount : (model.IsWindow === true ? 1 : 0)
                    effects: root.effects
                    dragging: root.dragRow === index && dragger.active
                    toolTipSub: launcher ? i18nd("jarvis-desktop", "Click to start · middle click: another window")
                              : windows > 1 ? i18nd("jarvis-desktop", "%1 windows: click to choose one", windows)
                              : i18nd("jarvis-desktop", "Click: show or minimize · middle click: new window")
                    toolTipActive: !windowsDialog.visible && !launcherDialog.visible
                    onActivated: {
                        if (launcher) tasks.requestNewInstance(root.modelIndex(index))
                        else if (model.IsGroupParent === true) root.showWindows(index, taskItem)
                        else if (active) tasks.requestToggleMinimized(root.modelIndex(index))
                        else tasks.requestActivate(root.modelIndex(index))
                    }
                    onNewInstance: tasks.requestNewInstance(root.modelIndex(index))
                    onMenuRequested: root.openTaskMenu(index, taskItem)
                    // Drag to reorder.
                    DragHandler {
                        id: dragger
                        target: null
                        grabPermissions: PointerHandler.CanTakeOverFromItems
                        onActiveChanged: {
                            if (active) root.dragRow = taskItem.index
                            else { root.dragRow = -1; tasks.syncLaunchers() }
                        }
                        onCentroidChanged: if (active) root.dragTo(root.dragRow, centroid.scenePosition)
                    }
                    // Files dropped on a running application open in it.
                    DropArea {
                        anchors.fill: parent
                        keys: ["text/uri-list"]
                        onEntered: taskItem.dropHover = true
                        onExited: taskItem.dropHover = false
                        onDropped: drop => {
                            taskItem.dropHover = false
                            const urls = drop.urls.filter(u => !String(u).endsWith(".desktop"))
                            if (urls.length > 0 && !taskItem.launcher) { tasks.requestOpenUrls(root.modelIndex(taskItem.index), urls); drop.accept() }
                        }
                    }
                }
            }
        }
    }
}
