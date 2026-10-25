import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.kicker as Kicker
import org.jarvis.visual as JV

// The launcher's browsing side: sections on the left (pinned, recent
// applications and files, all applications, the categories, places), their
// items on the right. Plasma's application models — the same ones Kickoff
// and the application menu use — so the list is what the system has
// installed (and updates when software is installed or removed).
FocusScope {
    id: browser
    property var appletInterface: null
    property var pinned: []
    signal launched()
    signal pinRequested(string url)
    signal unpinRequested(string url)
    signal typedText(string text)
    signal backToField()

    // The sections, as {key, label, icon, model}; built from the models' own rows.
    property var sections: []
    property int sectionIndex: 0
    readonly property var current: sections[sectionIndex] || null

    function storageIds() {
        return browser.pinned.filter(u => u.startsWith("applications:")).map(u => u.substring(13))
    }
    function rebuild() {
        const keep = current ? current.key : ""
        const list = [{ key: "pinned", label: i18nd("jarvis-desktop", "Pinned"), icon: "starred-symbolic", model: favorites }]
        for (let i = 0; i < rootModel.count; ++i) {
            const m = rootModel.modelForRow(i)
            if (!m || m.count === 0) continue
            list.push({ key: "root:" + rootModel.labelForRow(i), label: rootModel.labelForRow(i),
                        icon: rootModel.data(rootModel.index(i, 0), Qt.DecorationRole) || "applications-other", model: m })
        }
        if (places.count > 0)
            list.push({ key: "places", label: i18nd("jarvis-desktop", "Places and devices"), icon: "folder-open", model: places })
        sections = list
        const at = list.findIndex(s => s.key === keep)
        sectionIndex = at >= 0 ? at : 0
    }
    function reset() {
        rootModel.refresh()
        // Pinned first when there is something pinned; otherwise all applications.
        const allIndex = sections.findIndex(s => s.model !== favorites && s.model !== places && s.model.count > 20)
        sectionIndex = favorites.count > 0 ? 0 : Math.max(0, allIndex)
        grid.currentIndex = 0
        grid.positionViewAtBeginning()
    }
    function focusItems() { grid.forceActiveFocus(); if (grid.currentIndex < 0) grid.currentIndex = 0 }
    function runFirst() { if (grid.count > 0) trigger(Math.max(0, grid.currentIndex)) }
    function trigger(row) {
        const m = current ? current.model : null
        if (!m) return
        // A folder of the application tree opens in place.
        if (grid.currentItem && grid.currentItem.hasChildren) {
            const sub = m.modelForRow(row)
            if (sub) {
                const s = sections.slice()
                s.splice(sectionIndex + 1, 0, { key: "sub:" + grid.currentItem.label, label: grid.currentItem.label, icon: grid.currentItem.iconSource, model: sub })
                sections = s
                sectionIndex = sectionIndex + 1
            }
            return
        }
        if (m.trigger(row, "", null) !== false)
            browser.launched()
    }

    Kicker.RootModel {
        id: rootModel
        autoPopulate: false
        appletInterface: browser.appletInterface
        flat: false
        sorted: true
        showSeparators: false
        showTopLevelItems: false
        showAllApps: true
        showAllAppsCategorized: false
        showRecentApps: true
        showRecentDocs: true
        showPowerSession: false
        showFavoritesPlaceholder: false
        onCountChanged: Qt.callLater(browser.rebuild)
        Component.onCompleted: refresh()
    }
    // The dock's pinned applications, by their desktop file ids.
    Kicker.SimpleFavoritesModel {
        id: favorites
        favorites: browser.storageIds()
    }
    Kicker.ComputerModel {
        id: places
        appletInterface: browser.appletInterface
        appNameFormat: 0
        systemApplications: []
        onCountChanged: Qt.callLater(browser.rebuild)
    }

    // Sections.
    ListView {
        id: sectionList
        width: Math.round(parent.width * 0.27)
        height: parent.height
        clip: true
        model: browser.sections
        currentIndex: browser.sectionIndex
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }
        Keys.onTabPressed: browser.focusItems()
        Keys.onRightPressed: browser.focusItems()
        Keys.onReturnPressed: browser.focusItems()
        Keys.onUpPressed: event => { if (currentIndex === 0) browser.backToField(); else browser.sectionIndex = currentIndex - 1 }
        Keys.onDownPressed: browser.sectionIndex = Math.min(count - 1, currentIndex + 1)
        Keys.onPressed: event => {
            if (/^[^\x00-\x20\x7f]$/.test(event.text) && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                browser.typedText(event.text); event.accepted = true
            }
        }
        delegate: Rectangle {
            id: sectionRow
            required property int index
            required property var modelData
            readonly property bool selected: index === browser.sectionIndex
            width: sectionList.width - 6
            height: Math.round(Kirigami.Units.gridUnit * 1.9)
            radius: JV.JarvisTheme.radiusSmall
            color: selected ? JV.JarvisTheme.selectFill : sectionHover.hovered ? JV.JarvisTheme.hoverFill : "transparent"
            border.color: selected && sectionList.activeFocus ? JV.JarvisTheme.edgeStrong : "transparent"
            Rectangle {   // the light line of the current section
                visible: sectionRow.selected
                width: 2; height: parent.height - 12
                anchors.verticalCenter: parent.verticalCenter
                color: JV.JarvisTheme.cyan
            }
            Kirigami.Icon {
                id: sectionIcon
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                width: Kirigami.Units.iconSizes.small; height: width
                source: sectionRow.modelData.icon
            }
            Text {
                anchors.left: sectionIcon.right; anchors.leftMargin: 8
                anchors.right: parent.right; anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: sectionRow.modelData.label
                color: sectionRow.selected ? JV.JarvisTheme.text : JV.JarvisTheme.textSoft
                font.pixelSize: JV.JarvisTheme.fontBody
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            HoverHandler { id: sectionHover }
            TapHandler { onTapped: { browser.sectionIndex = sectionRow.index; grid.currentIndex = 0 } }
            Accessible.role: Accessible.PageTab
            Accessible.name: modelData.label
        }
    }
    Rectangle { x: sectionList.width + 4; width: 1; height: parent.height; color: JV.JarvisTheme.edge }

    // Items.
    GridView {
        id: grid
        x: sectionList.width + 12
        width: parent.width - x
        height: parent.height
        clip: true
        model: browser.current ? browser.current.model : null
        readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 6)))
        cellWidth: Math.floor(width / columns)
        cellHeight: Math.round(Kirigami.Units.gridUnit * 5.6)
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        currentIndex: 0
        QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }
        Keys.onReturnPressed: browser.trigger(currentIndex)
        Keys.onEnterPressed: browser.trigger(currentIndex)
        Keys.onSpacePressed: browser.trigger(currentIndex)
        Keys.onTabPressed: sectionList.forceActiveFocus()
        Keys.onBacktabPressed: sectionList.forceActiveFocus()
        Keys.onLeftPressed: event => { if (currentIndex % columns === 0) sectionList.forceActiveFocus(); else moveCurrentIndexLeft() }
        Keys.onUpPressed: event => { if (currentIndex < columns) browser.backToField(); else moveCurrentIndexUp() }
        Keys.onMenuPressed: if (currentItem) currentItem.openMenu()
        Keys.onPressed: event => {
            if (/^[^\x00-\x20\x7f]$/.test(event.text) && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                browser.typedText(event.text); event.accepted = true
            }
        }
        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } }
        delegate: Item {
            id: tile
            required property int index
            required property var model
            readonly property string label: model.display || ""
            readonly property var iconSource: model.decoration
            readonly property bool hasChildren: model.hasChildren === true
            readonly property string favoriteId: model.favoriteId || ""
            readonly property bool isApp: favoriteId.endsWith(".desktop")
            readonly property bool selected: GridView.isCurrentItem && grid.activeFocus
            width: grid.cellWidth
            height: grid.cellHeight
            function openMenu() {
                tileMenu.popup(tile, tile.width / 2, tile.height / 2)
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: JV.JarvisTheme.radiusSmall + 2
                color: tile.selected ? JV.JarvisTheme.selectFill : tileHover.hovered ? JV.JarvisTheme.hoverFill : "transparent"
                border.color: tile.selected ? JV.JarvisTheme.edgeStrong : tileHover.hovered ? JV.JarvisTheme.edge : "transparent"
                Behavior on color { ColorAnimation { duration: 120 } }
            }
            Kirigami.Icon {
                id: tileIcon
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.round(parent.height * 0.12)
                width: Kirigami.Units.iconSizes.large; height: width
                source: tile.iconSource
                scale: tileHover.hovered ? 1.08 : 1
                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            }
            Text {
                anchors.top: tileIcon.bottom; anchors.topMargin: 6
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 6; anchors.rightMargin: 6
                horizontalAlignment: Text.AlignHCenter
                text: tile.label
                color: JV.JarvisTheme.text
                font.pixelSize: JV.JarvisTheme.fontSmall + 1
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            // A pinned application: a small star.
            Text {
                visible: tile.isApp && browser.pinned.indexOf("applications:" + tile.favoriteId) >= 0
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                text: "★"; color: JV.JarvisTheme.amber; font.pixelSize: JV.JarvisTheme.fontSmall
            }
            HoverHandler { id: tileHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: { grid.currentIndex = tile.index; browser.trigger(tile.index) }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: { grid.currentIndex = tile.index; tile.openMenu() }
            }
            QQC2.ToolTip.visible: tileHover.hovered && (model.description || "").length > 0
            QQC2.ToolTip.delay: 600
            QQC2.ToolTip.text: model.description || ""
            Accessible.role: Accessible.Button
            Accessible.name: label
            Accessible.description: model.description || ""
            Accessible.onPressAction: browser.trigger(index)

            QQC2.Menu {
                id: tileMenu
                QQC2.MenuItem {
                    text: tile.hasChildren ? i18nd("jarvis-desktop", "Open") : i18nd("jarvis-desktop", "Start")
                    icon.name: "media-playback-start"
                    onTriggered: browser.trigger(tile.index)
                }
                QQC2.MenuItem {
                    visible: tile.isApp
                    height: visible ? implicitHeight : 0
                    readonly property bool isPinned: browser.pinned.indexOf("applications:" + tile.favoriteId) >= 0
                    text: isPinned ? i18nd("jarvis-desktop", "Unpin from the dock") : i18nd("jarvis-desktop", "Pin to the dock")
                    icon.name: isPinned ? "window-unpin" : "window-pin"
                    onTriggered: isPinned ? browser.unpinRequested("applications:" + tile.favoriteId)
                                          : browser.pinRequested("applications:" + tile.favoriteId)
                }
            }
        }
        Text {
            visible: grid.count === 0
            anchors.centerIn: parent
            width: parent.width * 0.8
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: JV.JarvisTheme.textSoft
            font.pixelSize: JV.JarvisTheme.fontBody
            text: browser.current && browser.current.key === "pinned"
                  ? i18nd("jarvis-desktop", "Nothing pinned yet. Right click an application → Pin to the dock.")
                  : i18nd("jarvis-desktop", "Nothing here yet.")
        }
    }
}
