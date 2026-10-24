import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.milou as Milou
import org.jarvis.visual as JV

// The launcher's search: KRunner's results (Milou's model) — every runner the
// user has enabled: applications, files, settings, the calculator, units,
// Jarvis's runner ("jarvis <question>", "switch to dev", "open kicad"). A
// result runs through KRunner itself.
FocusScope {
    id: results
    property string query: ""
    signal launched()
    signal typedText(string text)
    signal backToField()

    function focusItems() { list.forceActiveFocus(); if (list.currentIndex < 0) list.currentIndex = 0 }
    function runCurrent() {
        if (list.count === 0)
            return false
        return model.run(model.index(Math.max(0, list.currentIndex), 0))
    }

    Milou.ResultsModel {
        id: model
        queryString: results.query
        limit: 24
        onQueryStringChanged: list.currentIndex = 0
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: model
        highlightMoveDuration: 0
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }
        Keys.onReturnPressed: if (results.runCurrent()) results.launched()
        Keys.onEnterPressed: if (results.runCurrent()) results.launched()
        Keys.onUpPressed: event => { if (currentIndex <= 0) results.backToField(); else decrementCurrentIndex() }
        Keys.onTabPressed: results.backToField()
        Keys.onPressed: event => {
            if (/^[^\x00-\x20\x7f]$/.test(event.text) && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                results.typedText(event.text); event.accepted = true
            }
        }
        section.property: "category"
        section.delegate: Text {
            required property string section
            width: list.width
            text: section.toUpperCase()
            color: JV.JarvisTheme.amber
            font.family: JV.JarvisTheme.mono; font.pixelSize: JV.JarvisTheme.fontSmall; font.letterSpacing: 2
            topPadding: 8; bottomPadding: 2
            elide: Text.ElideRight
        }
        delegate: Rectangle {
            id: row
            required property int index
            required property var model
            width: list.width - 8
            height: Math.round(Kirigami.Units.gridUnit * 2.6)
            radius: JV.JarvisTheme.radiusSmall
            color: ListView.isCurrentItem ? JV.JarvisTheme.selectFill : hover.hovered ? JV.JarvisTheme.hoverFill : "transparent"
            border.color: ListView.isCurrentItem ? JV.JarvisTheme.edgeStrong : "transparent"
            Kirigami.Icon {
                id: rowIcon
                x: 8; anchors.verticalCenter: parent.verticalCenter
                width: Kirigami.Units.iconSizes.medium; height: width
                source: row.model.decoration
            }
            Column {
                anchors.left: rowIcon.right; anchors.leftMargin: 10
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                Text { width: parent.width; text: row.model.display || ""; color: JV.JarvisTheme.text; font.pixelSize: JV.JarvisTheme.fontBody; elide: Text.ElideRight; textFormat: Text.PlainText }
                Text { width: parent.width; text: String(row.model.subtext || ""); visible: text.length > 0; color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall; elide: Text.ElideRight; textFormat: Text.PlainText }
            }
            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                onTapped: {
                    list.currentIndex = row.index
                    if (results.runCurrent()) results.launched()
                }
            }
            Accessible.role: Accessible.Button
            Accessible.name: model.display || ""
        }
        Text {
            visible: list.count === 0
            anchors.centerIn: parent
            width: parent.width * 0.85
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: JV.JarvisTheme.textSoft
            font.pixelSize: JV.JarvisTheme.fontBody
            text: i18nd("jarvis-desktop", "Nothing found. To ask Jarvis, start with “jarvis”: jarvis how much RAM is free")
        }
    }
}
