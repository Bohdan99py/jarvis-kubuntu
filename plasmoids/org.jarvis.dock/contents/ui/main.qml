import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import org.kde.plasma.workspace.dbus as DBus
import org.jarvis.visual as JV

// Jarvis Dock (0.19.2): a task bar for a Plasma panel in the Jarvis look,
// on Plasma's own task model — the same windows, launchers, grouping and
// requests as the standard task manager. Jarvis's parts (the button, the
// workspaces) talk to jarvisd; everything else works without it, so a
// stopped Jarvis never takes the task bar away. The system tray and the
// clock beside it are Plasma's own widgets (the desktop shell puts them in
// the same panel).
PlasmoidItem {
    id: root
    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
    readonly property int cell: Math.max(24, vertical ? width : height) - 4
    readonly property bool effects: Plasmoid.configuration.effects && link.desktop.effects !== false

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    Plasmoid.constraintHints: Plasmoid.CanFillArea
    preferredRepresentation: fullRepresentation

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
        groupMode: TaskManager.TasksModel.GroupApplications
        groupInline: false
        separateLaunchers: false
        launchInPlace: true
        filterByVirtualDesktop: true
        filterByActivity: true
        filterByScreen: Plasmoid.configuration.filterByScreen
        screenGeometry: Plasmoid.containment ? Plasmoid.containment.screenGeometry : Qt.rect(0, 0, 0, 0)
        virtualDesktop: virtualDesktops.currentDesktop
        activity: activityInfo.currentActivity
        launcherList: Plasmoid.configuration.launchers
        onLauncherListChanged: Plasmoid.configuration.launchers = launcherList
    }
    function modelIndex(i) { return tasks.makeModelIndex(i) }
    function switchDesktop(number) {
        DBus.SessionBus.asyncCall({ service: "org.kde.KWin", path: "/KWin", interface: "org.kde.KWin", member: "setCurrentDesktop",
                                    arguments: [number], signature: "(i)" }, () => {}, () => {})
    }

    QQC2.Menu {
        id: taskMenu
        property int row: -1
        property var info: ({})
        QQC2.MenuItem {
            text: i18nd("jarvis-desktop", "New window")
            icon.name: "window-new"
            onTriggered: tasks.requestNewInstance(root.modelIndex(taskMenu.row))
        }
        QQC2.MenuItem {
            text: taskMenu.info.pinned ? i18nd("jarvis-desktop", "Unpin from the dock") : i18nd("jarvis-desktop", "Pin to the dock")
            icon.name: taskMenu.info.pinned ? "window-unpin" : "window-pin"
            enabled: !!taskMenu.info.url
            onTriggered: taskMenu.info.pinned ? tasks.requestRemoveLauncher(taskMenu.info.url) : tasks.requestAddLauncher(taskMenu.info.url)
        }
        QQC2.MenuSeparator { visible: taskMenu.info.window === true }
        QQC2.MenuItem {
            visible: taskMenu.info.window === true
            text: taskMenu.info.minimized ? i18nd("jarvis-desktop", "Restore") : i18nd("jarvis-desktop", "Minimize")
            icon.name: "window-minimize"
            onTriggered: tasks.requestToggleMinimized(root.modelIndex(taskMenu.row))
        }
        QQC2.MenuItem {
            visible: taskMenu.info.window === true
            text: i18nd("jarvis-desktop", "Maximize or restore")
            icon.name: "window-maximize"
            onTriggered: tasks.requestToggleMaximized(root.modelIndex(taskMenu.row))
        }
        QQC2.MenuItem {
            visible: taskMenu.info.window === true
            text: taskMenu.info.windows > 1 ? i18nd("jarvis-desktop", "Close all %1 windows", taskMenu.info.windows) : i18nd("jarvis-desktop", "Close")
            icon.name: "window-close"
            onTriggered: tasks.requestClose(root.modelIndex(taskMenu.row))
        }
    }

    fullRepresentation: Item {
        id: dock
        Layout.minimumWidth: root.vertical ? -1 : row.implicitWidth + 16
        Layout.preferredWidth: root.vertical ? -1 : row.implicitWidth + 16
        Layout.minimumHeight: root.vertical ? row.implicitHeight + 16 : -1
        Layout.preferredHeight: root.vertical ? row.implicitHeight + 16 : -1
        Layout.fillWidth: false
        Layout.fillHeight: false

        PlasmaCore.Dialog {
            id: launcherDialog
            visualParent: launcherButton
            location: Plasmoid.location
            hideOnWindowDeactivate: true
            flags: Qt.WindowStaysOnTopHint
            type: PlasmaCore.Dialog.PopupMenu
            backgroundHints: PlasmaCore.Dialog.NoBackground
            mainItem: LauncherView {
                id: launcherView
                onDone: launcherDialog.visible = false
            }
            onVisibleChanged: if (visible) launcherView.reset()
        }

        // The dock's surface: dark glass, a thin holographic edge, corner ticks.
        Rectangle {
            id: surface
            anchors.fill: parent
            anchors.margins: 1
            radius: 10
            color: JV.JarvisTheme.glass
            border.color: JV.JarvisTheme.line
            opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity { enabled: root.effects; NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    width: index % 2 ? 2 : 10; height: index % 2 ? 10 : 2
                    color: JV.JarvisTheme.cyan
                    opacity: 0.8
                    x: index < 2 ? 4 : surface.width - width - 4
                    y: index < 2 ? 4 : surface.height - height - 4
                }
            }
        }

        Grid {
            id: row
            anchors.centerIn: parent
            rows: root.vertical ? 0 : 1
            columns: root.vertical ? 1 : 0
            spacing: Math.round(4 * JV.JarvisTheme.spacing)
            verticalItemAlignment: Grid.AlignVCenter
            horizontalItemAlignment: Grid.AlignHCenter

            // Jarvis: the overlay on a click; its state colour; a flash on notifications.
            Item {
                id: jarvisButton
                width: root.cell; height: root.cell
                function flash() { if (root.effects) flashAnim.restart() }
                JV.DesktopCore {
                    id: miniCore
                    anchors.fill: parent
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
                    SequentialAnimation {
                        id: flashAnim
                        OpacityAnimator { target: flashRing; from: 1; to: 0; duration: 900 }
                    }
                }
                MouseArea {
                    id: jarvisMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouse => {
                        if (!link.online) link.startJarvis()
                        else if (mouse.button === Qt.RightButton) link.run("main")
                        else link.run("overlay")
                    }
                }
                QQC2.ToolTip.visible: jarvisMouse.containsMouse
                QQC2.ToolTip.text: link.online ? i18nd("jarvis-desktop", "Jarvis — click: overlay, right click: the window") : i18nd("jarvis-desktop", "Start Jarvis")
                Accessible.role: Accessible.Button
                Accessible.name: "Jarvis"
            }
            // The launcher.
            Rectangle {
                id: launcherButton
                width: root.cell * 0.8; height: width; radius: width / 2
                color: launcherMouse.containsMouse || launcherDialog.visible ? Qt.rgba(JV.JarvisTheme.cyan.r, JV.JarvisTheme.cyan.g, JV.JarvisTheme.cyan.b, 0.2) : "transparent"
                border.color: JV.JarvisTheme.line
                Kirigami.Icon { anchors.centerIn: parent; width: parent.width * 0.6; height: width; source: "search"; color: JV.JarvisTheme.cyan }
                MouseArea { id: launcherMouse; anchors.fill: parent; hoverEnabled: true; onClicked: launcherDialog.visible = !launcherDialog.visible }
                Accessible.role: Accessible.Button
                Accessible.name: i18nd("jarvis-desktop", "Launcher")
            }
            // Workspaces (from jarvisd) and the virtual desktops (from KWin).
            Repeater {
                model: Plasmoid.configuration.showProfiles && link.integrated && link.status.workspace ? link.status.workspace.profiles : []
                JV.HoloButton {
                    required property var modelData
                    width: root.vertical ? root.cell : implicitWidth
                    height: root.cell * 0.62
                    text: root.vertical ? modelData.label.charAt(0) : modelData.label
                    checked: link.status.workspace.current === modelData.id
                    onClicked: link.workspace(modelData.id)
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: i18nd("jarvis-desktop", "Workspace %1 · desktop %2", modelData.label, modelData.desktop)
                }
            }
            Repeater {
                model: Plasmoid.configuration.showProfiles && virtualDesktops.numberOfDesktops > 1 ? virtualDesktops.desktopIds : []
                Rectangle {
                    required property int index
                    required property var modelData
                    readonly property bool current: virtualDesktops.currentDesktop === modelData
                    width: 9; height: 9; radius: 2
                    rotation: 45
                    color: current ? JV.JarvisTheme.cyan : "transparent"
                    border.color: JV.JarvisTheme.lineStrong
                    MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: root.switchDesktop(index + 1) }
                    Accessible.role: Accessible.Button
                    Accessible.name: virtualDesktops.desktopNames[index] || String(index + 1)
                }
            }
            Rectangle { width: root.vertical ? root.cell * 0.6 : 1; height: root.vertical ? 1 : root.cell * 0.6; color: JV.JarvisTheme.line }
            // The applications: pinned launchers and running windows, grouped per application.
            Repeater {
                model: tasks
                DockTask {
                    required property int index
                    required property var model
                    size: root.cell
                    name: model.display || ""
                    icon: model.decoration
                    launcher: model.IsLauncher === true
                    active: model.IsActive === true
                    minimized: model.IsMinimized === true
                    attention: model.IsDemandingAttention === true
                    starting: model.IsStartup === true
                    windows: model.IsGroupParent === true ? model.ChildCount : (model.IsWindow === true ? 1 : 0)
                    effects: root.effects
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.text: name
                    onActivated: {
                        if (launcher) tasks.requestNewInstance(root.modelIndex(index))
                        else if (active && model.IsGroupParent !== true) tasks.requestToggleMinimized(root.modelIndex(index))
                        else tasks.requestActivate(root.modelIndex(index))
                    }
                    onNewInstance: tasks.requestNewInstance(root.modelIndex(index))
                    onMenuRequested: {
                        const url = model.LauncherUrlWithoutIcon
                        taskMenu.row = index
                        taskMenu.info = { url: url, pinned: url ? tasks.launcherPosition(url) >= 0 : false,
                                          window: model.IsWindow === true || model.IsGroupParent === true,
                                          minimized: model.IsMinimized === true, windows: model.IsGroupParent === true ? model.ChildCount : 1 }
                        taskMenu.popup()
                    }
                }
            }
        }
    }
}
