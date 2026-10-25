import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.jarvis.visual as JV

// The JARVIS launcher (0.19.3): the dock's start menu. Real data only, from
// the desktop's own mechanisms:
//   · the applications, their categories, recent applications and files —
//     Plasma's application models (the ones Kickoff uses, AppBrowser.qml);
//   · the places (home, documents, devices) — the same models;
//   · search — KRunner (Milou): applications, files, settings, the
//     calculator and Jarvis's runner ("jarvis …", "switch to dev");
//   · lock, log out, restart, shut down — Plasma's session actions.
// Starting anything goes through those models (KIO's launcher jobs, each
// application in its own scope); nothing here builds a command line.
// Keyboard: type to search, arrows to move, Enter to start, Tab between the
// categories and the items, Escape clears the search or closes.
FocusScope {
    id: launcher
    property var appletInterface: null
    property var pinned: []              // the dock's launcher urls (applications:x.desktop)
    property bool jarvisOnline: false
    property bool effects: true
    signal done()
    signal pinRequested(string url)
    signal unpinRequested(string url)
    signal jarvisRequested()

    readonly property bool searching: field.text.length > 0
    // The application models load on the first opening, not with the panel (memory).
    property bool opened: false
    implicitWidth: Kirigami.Units.gridUnit * 40
    implicitHeight: Kirigami.Units.gridUnit * 30

    // Opens (again): empty search, the first section, focus in the field.
    function reset() {
        opened = true
        field.text = ""
        if (browser.item) browser.item.reset()
        field.forceActiveFocus()
        reveal.restart()
    }
    function runSearch() {
        if (search.item && search.item.runCurrent())
            launcher.done()
    }
    function launchedItem() { launcher.done() }
    function isPinned(storageId) { return launcher.pinned.indexOf("applications:" + storageId) >= 0 }

    Keys.onEscapePressed: event => {
        if (launcher.searching) field.text = ""
        else launcher.done()
        event.accepted = true
    }

    // The glass: rounded, a thin edge, a dark outline that keeps it visible on light wallpapers.
    Rectangle {
        id: glass
        anchors.fill: parent
        radius: JV.JarvisTheme.radius + 2
        color: JV.JarvisTheme.surface
        border.color: JV.JarvisTheme.edge
        Rectangle {
            anchors.fill: parent; anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"; border.color: JV.JarvisTheme.outline; z: -1
        }
        // Corner ticks: the HUD's signature, kept small.
        Repeater {
            model: 4
            Item {
                required property int index
                width: 14; height: 14
                x: index % 2 ? glass.width - width - 6 : 6
                y: index < 2 ? 6 : glass.height - height - 6
                Rectangle { width: 14; height: 2; color: JV.JarvisTheme.cyan; opacity: 0.8; y: parent.index < 2 ? 0 : 12 }
                Rectangle { width: 2; height: 14; color: JV.JarvisTheme.cyan; opacity: 0.8; x: parent.index % 2 ? 12 : 0 }
            }
        }
        // A light sweep when it opens.
        Rectangle {
            id: sweep
            width: parent.width; height: 2
            color: JV.JarvisTheme.cyan
            opacity: 0
            visible: opacity > 0
        }
        SequentialAnimation {
            id: reveal
            ParallelAnimation {
                NumberAnimation { target: sweep; property: "y"; from: 0; to: glass.height; duration: launcher.effects ? 420 : 0; easing.type: Easing.OutCubic }
                NumberAnimation { target: sweep; property: "opacity"; from: launcher.effects ? 0.6 : 0; to: 0; duration: launcher.effects ? 420 : 0 }
                NumberAnimation { target: body; property: "opacity"; from: launcher.effects ? 0 : 1; to: 1; duration: launcher.effects ? 260 : 0 }
            }
        }
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // Title, Jarvis.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "J.A.R.V.I.S. · " + i18nd("jarvis-desktop", "LAUNCHER")
                color: JV.JarvisTheme.cyan
                font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontTitle; font.letterSpacing: 3
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            JV.HoloButton {
                glyph: "◎"
                text: launcher.jarvisOnline ? i18nd("jarvis-desktop", "Ask Jarvis") : i18nd("jarvis-desktop", "Start Jarvis")
                primary: true
                onClicked: { launcher.jarvisRequested(); launcher.done() }
            }
        }

        // The search field.
        QQC2.TextField {
            id: field
            Layout.fillWidth: true
            focus: true
            font.pixelSize: JV.JarvisTheme.fontBody
            leftPadding: Kirigami.Units.iconSizes.small + 16
            placeholderText: i18nd("jarvis-desktop", "Search applications, files, settings… or type: jarvis <question>")
            placeholderTextColor: JV.JarvisTheme.textSoft
            color: JV.JarvisTheme.text
            background: Rectangle {
                radius: JV.JarvisTheme.radiusSmall
                color: Qt.rgba(0, 0, 0, 0.35)
                border.color: field.activeFocus ? JV.JarvisTheme.edgeStrong : JV.JarvisTheme.edge
                Kirigami.Icon {
                    source: "search"
                    width: Kirigami.Units.iconSizes.small; height: width
                    anchors.left: parent.left; anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: JV.JarvisTheme.cyan
                }
            }
            Accessible.name: i18nd("jarvis-desktop", "Search")
            Keys.onDownPressed: (launcher.searching ? search.item : browser.item)?.focusItems()
            Keys.onTabPressed: (launcher.searching ? search.item : browser.item)?.focusItems()
            Keys.onReturnPressed: launcher.searching ? launcher.runSearch() : browser.item?.runFirst()
            Keys.onEnterPressed: launcher.searching ? launcher.runSearch() : browser.item?.runFirst()
        }

        // Browse (categories and items) or search results.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Loader {
                id: browser
                anchors.fill: parent
                visible: !launcher.searching
                source: "AppBrowser.qml"
                active: launcher.opened
                onLoaded: item.reset()
                // Without Plasma's application models (a stripped install) the search still works.
                onStatusChanged: if (status === Loader.Error) console.warn("Jarvis launcher: application browser unavailable; search only")
            }
            Connections {
                target: browser.item
                ignoreUnknownSignals: true
                function onLaunched() { launcher.done() }
                function onPinRequested(url) { launcher.pinRequested(url) }
                function onUnpinRequested(url) { launcher.unpinRequested(url) }
                function onTypedText(text) { field.text += text; field.forceActiveFocus() }
                function onBackToField() { field.forceActiveFocus() }
            }
            Binding { target: browser.item; property: "appletInterface"; value: launcher.appletInterface; when: browser.status === Loader.Ready }
            Binding { target: browser.item; property: "pinned"; value: launcher.pinned; when: browser.status === Loader.Ready }
            Text {
                visible: launcher.opened && browser.status === Loader.Error && !launcher.searching
                anchors.centerIn: parent
                width: parent.width * 0.8
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: JV.JarvisTheme.textSoft
                font.pixelSize: JV.JarvisTheme.fontBody
                text: i18nd("jarvis-desktop", "Plasma's application list is not available here. Type to search applications.")
            }
            Loader {
                id: search
                anchors.fill: parent
                visible: launcher.searching
                active: launcher.opened
                source: "SearchResults.qml"
                Binding { target: search.item; property: "query"; value: field.text; when: search.status === Loader.Ready }
            }
            Connections {
                target: search.item
                ignoreUnknownSignals: true
                function onLaunched() { launcher.done() }
                function onTypedText(text) { field.text += text; field.forceActiveFocus() }
                function onBackToField() { field.forceActiveFocus() }
            }
            Text {
                visible: launcher.opened && search.status === Loader.Error && launcher.searching
                anchors.centerIn: parent
                color: JV.JarvisTheme.textSoft
                font.pixelSize: JV.JarvisTheme.fontBody
                text: i18nd("jarvis-desktop", "KRunner search is not installed (package milou).")
            }
        }

        // Places and the session: what a start menu has at the bottom.
        Loader {
            id: session
            Layout.fillWidth: true
            source: "SessionBar.qml"
            active: launcher.opened
            visible: status === Loader.Ready
            Connections {
                target: session.item
                ignoreUnknownSignals: true
                function onLaunched() { launcher.done() }
            }
        }
    }
}
