import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.milou as Milou
import org.jarvis.visual as JV

// The Jarvis launcher: KRunner's own search (Milou's results model — every
// runner the user has: applications, files, settings, the calculator, and
// Jarvis's runner for workspaces, projects, figures and questions) in the
// Jarvis look. No search of its own; local operations need no network.
FocusScope {
    id: launcher
    signal done()
    implicitWidth: Kirigami.Units.gridUnit * 30
    implicitHeight: Kirigami.Units.gridUnit * 24

    function reset() {
        field.text = ""
        field.forceActiveFocus()
    }
    function runCurrent() {
        if (list.count === 0)
            return
        if (results.run(results.index(Math.max(0, list.currentIndex), 0)))
            launcher.done()
    }

    Rectangle {
        anchors.fill: parent
        color: JV.JarvisTheme.glassStrong
        border.color: JV.JarvisTheme.line
        radius: 10
    }
    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Text {
            text: "J.A.R.V.I.S. · " + i18nd("jarvis-desktop", "LAUNCHER")
            color: JV.JarvisTheme.cyan
            font.family: JV.JarvisTheme.mono; font.pixelSize: 11; font.letterSpacing: 3
        }
        QQC2.TextField {
            id: field
            width: parent.width
            focus: true
            placeholderText: i18nd("jarvis-desktop", "Open an application, a file, a project… or ask Jarvis")
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onReturnPressed: launcher.runCurrent()
            Keys.onEnterPressed: launcher.runCurrent()
            Keys.onEscapePressed: launcher.done()
        }
        Text {
            visible: field.text.length === 0
            width: parent.width
            wrapMode: Text.Wrap
            color: JV.JarvisTheme.textDim
            font.pixelSize: 11
            text: i18nd("jarvis-desktop", "Try: open kicad · switch to study · find project esp32 · show gpu load · open the downloads folder · jarvis <question>")
        }
        ListView {
            id: list
            width: parent.width
            height: parent.height - field.height - 40
            clip: true
            model: Milou.ResultsModel {
                id: results
                queryString: field.text
                limit: 14
                onQueryStringChanged: list.currentIndex = 0
            }
            highlightMoveDuration: 0
            section.property: "category"
            section.delegate: Text {
                required property string section
                text: section.toUpperCase()
                color: JV.JarvisTheme.amber
                font.family: JV.JarvisTheme.mono; font.pixelSize: 10; font.letterSpacing: 2
                topPadding: 6
            }
            delegate: Rectangle {
                id: row
                required property int index
                required property var model
                width: list.width
                height: Kirigami.Units.gridUnit * 2.2
                radius: 6
                color: ListView.isCurrentItem || hover.hovered ? Qt.rgba(JV.JarvisTheme.cyan.r, JV.JarvisTheme.cyan.g, JV.JarvisTheme.cyan.b, 0.14) : "transparent"
                border.color: ListView.isCurrentItem ? JV.JarvisTheme.line : "transparent"
                Kirigami.Icon {
                    id: rowIcon
                    x: 6; anchors.verticalCenter: parent.verticalCenter
                    width: Kirigami.Units.iconSizes.medium; height: width
                    source: row.model.decoration
                }
                Column {
                    anchors.left: rowIcon.right; anchors.leftMargin: 8
                    anchors.right: parent.right; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Text { width: parent.width; text: row.model.display || ""; color: JV.JarvisTheme.text; font.pixelSize: 13; elide: Text.ElideRight; textFormat: Text.PlainText }
                    Text { width: parent.width; text: String(row.model.subtext || ""); visible: text.length > 0; color: JV.JarvisTheme.textDim; font.pixelSize: 10; elide: Text.ElideRight; textFormat: Text.PlainText }
                }
                HoverHandler { id: hover }
                TapHandler {
                    onTapped: {
                        list.currentIndex = row.index
                        launcher.runCurrent()
                    }
                }
            }
        }
    }
}
