import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.kicker as Kicker
import org.jarvis.visual as JV

// The launcher's bottom line: Plasma's session actions (lock, log out, switch
// user, sleep, restart, shut down — whatever this system allows; Plasma asks
// before logging out or shutting down, as from its own menu).
Item {
    id: bar
    signal launched()
    implicitHeight: Math.round(Kirigami.Units.gridUnit * 2.2)

    Kicker.SystemModel { id: system }

    Rectangle { width: parent.width; height: 1; color: JV.JarvisTheme.edge }
    Row {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 3
        spacing: 4
        Repeater {
            model: system
            Rectangle {
                id: action
                required property int index
                required property var model
                width: height; height: Math.round(Kirigami.Units.gridUnit * 1.8)
                radius: width / 2
                color: hover.hovered || activeFocus ? JV.JarvisTheme.hoverFill : "transparent"
                border.color: hover.hovered || activeFocus ? JV.JarvisTheme.edgeStrong : JV.JarvisTheme.edge
                activeFocusOnTab: true
                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Kirigami.Units.iconSizes.small; height: width
                    source: action.model.decoration
                    color: JV.JarvisTheme.text
                }
                function run() { if (system.trigger(index, "", null) !== false) bar.launched() }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: action.run() }
                Keys.onReturnPressed: run()
                Keys.onSpacePressed: run()
                QQC2.ToolTip.visible: hover.hovered
                QQC2.ToolTip.text: model.display || ""
                Accessible.role: Accessible.Button
                Accessible.name: model.display || ""
                Accessible.onPressAction: run()
            }
        }
    }
    Text {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 3
        width: parent.width * 0.5
        text: i18nd("jarvis-desktop", "Enter start · ↑↓←→ move · Tab sections · Esc close")
        color: JV.JarvisTheme.textSoft
        font.pixelSize: JV.JarvisTheme.fontSmall
        elide: Text.ElideRight
    }
}
