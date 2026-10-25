import QtQuick
import QtQml.Models
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import org.jarvis.visual as JV

// The windows of one application (a group in the dock): title, the active one
// marked, minimized ones dimmed; a click activates, the cross closes, the
// keyboard works too (↑↓, Enter, Delete, Escape). Plasma's task model is the
// one source, so the list follows windows opening and closing.
FocusScope {
    id: list
    property var tasksModel: null
    property int row: -1
    signal done()

    implicitWidth: Kirigami.Units.gridUnit * 22
    implicitHeight: Math.min(Kirigami.Units.gridUnit * 20, view.contentHeight + header.height + 28)

    function childIndex(i) { return tasksModel.makeModelIndex(row, i) }
    function activate(i) { tasksModel.requestActivate(childIndex(i)); list.done() }

    Keys.onEscapePressed: list.done()
    onActiveFocusChanged: if (activeFocus) view.forceActiveFocus()

    Rectangle {
        anchors.fill: parent
        radius: JV.JarvisTheme.radius
        color: JV.JarvisTheme.surface
        border.color: JV.JarvisTheme.edge
        Rectangle { anchors.fill: parent; anchors.margins: -1; radius: parent.radius + 1; color: "transparent"; border.color: JV.JarvisTheme.outline; z: -1 }
    }
    Text {
        id: header
        x: 14; y: 10
        width: parent.width - 28
        text: list.row >= 0 && list.tasksModel ? (list.tasksModel.data(list.tasksModel.makeModelIndex(list.row), Qt.DisplayRole) || "").toUpperCase() : ""
        color: JV.JarvisTheme.cyan
        font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontTitle; font.letterSpacing: 2
        elide: Text.ElideRight
    }
    ListView {
        id: view
        anchors.top: header.bottom; anchors.topMargin: 8
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 8
        clip: true
        focus: true
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        boundsBehavior: Flickable.StopAtBounds
        model: DelegateModel {
            id: windowsModel
            model: list.tasksModel
            rootIndex: list.tasksModel ? list.tasksModel.makeModelIndex(list.row) : windowsModel.modelIndex(-1)
            delegate: Rectangle {
                id: entry
                required property int index
                required property var model
                width: view.width
                height: Math.round(Kirigami.Units.gridUnit * 2.2)
                radius: JV.JarvisTheme.radiusSmall
                color: ListView.isCurrentItem ? JV.JarvisTheme.selectFill : hover.hovered ? JV.JarvisTheme.hoverFill : "transparent"
                border.color: entry.model.IsActive === true ? JV.JarvisTheme.edgeStrong : "transparent"
                Kirigami.Icon {
                    id: winIcon
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: Kirigami.Units.iconSizes.smallMedium; height: width
                    source: entry.model.decoration
                    opacity: entry.model.IsMinimized === true ? 0.5 : 1
                }
                Text {
                    anchors.left: winIcon.right; anchors.leftMargin: 8
                    anchors.right: closeButton.left; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: (entry.model.display || "") + (entry.model.IsMinimized === true ? "  · " + i18nd("jarvis-desktop", "minimized") : "")
                    color: entry.model.IsMinimized === true ? JV.JarvisTheme.textSoft : JV.JarvisTheme.text
                    font.pixelSize: JV.JarvisTheme.fontBody
                    font.bold: entry.model.IsActive === true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Rectangle {
                    id: closeButton
                    anchors.right: parent.right; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: Kirigami.Units.iconSizes.smallMedium; height: width; radius: width / 2
                    color: closeHover.hovered ? JV.JarvisTheme.alpha(JV.JarvisTheme.red, 0.3) : "transparent"
                    Text { anchors.centerIn: parent; text: "✕"; color: JV.JarvisTheme.text; font.pixelSize: JV.JarvisTheme.fontSmall }
                    HoverHandler { id: closeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: list.tasksModel.requestClose(list.childIndex(entry.index)) }
                    Accessible.role: Accessible.Button
                    Accessible.name: i18nd("jarvis-desktop", "Close")
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: list.activate(entry.index) }
                Accessible.role: Accessible.Button
                Accessible.name: entry.model.display || ""
            }
        }
        Keys.onReturnPressed: list.activate(currentIndex)
        Keys.onEnterPressed: list.activate(currentIndex)
        Keys.onDeletePressed: list.tasksModel.requestClose(list.childIndex(currentIndex))
        onCountChanged: if (count === 0) list.done()
    }
}
