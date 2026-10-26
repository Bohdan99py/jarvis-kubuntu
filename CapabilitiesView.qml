import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// "What Jarvis can do" (0.22.1): the actions Jarvis really carries out, built
// from the tool registry (agent::capabilityCatalogue) — a guide, not a list of
// required phrases. Each row says whether it works offline, whether Jarvis asks
// first and whether it is available on this computer now (and why not). The
// search is local: titles, descriptions and the Russian and English examples.
// A click on an example only puts it into the message field for editing:
// browsing this list never changes anything on the system.
ColumnLayout {
    id: view
    required property var panel          // AgentPanel
    property bool english: false
    signal exampleChosen(string text)

    spacing: 8
    Component.onCompleted: panel.loadCapabilities()

    function matches(row, query) {
        const q = query.trim().toLowerCase().replace(/ё/g, "е")
        if (q.length === 0)
            return true
        const hay = [row.title, row.text, row.sectionTitle].concat(row.examplesRu, row.examplesEn).join("\n").toLowerCase().replace(/ё/g, "е")
        return q.split(/\s+/).every(w => hay.indexOf(w) >= 0)
    }
    readonly property var rows: {
        const out = []
        for (let i = 0; i < panel.capabilities.length; ++i)
            if (matches(panel.capabilities[i], search.text))
                out.push(panel.capabilities[i])
        return out
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textDim
        text: qsTr("Say what you want in your own words — these are examples, not commands to learn. Everything here runs on this computer, without the internet. Click an example to put it into the message field.")
    }
    TextField {
        id: search
        objectName: "capabilitiesSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search: volume, headphones, window, яркость…")
        Accessible.name: qsTr("Search the capabilities")
    }
    Label {
        visible: view.rows.length === 0
        text: qsTr("Nothing found. Ask in your own words anyway — Jarvis tells you honestly if it cannot do it.")
        color: Theme.textDim
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Repeater {
        model: view.rows
        delegate: ColumnLayout {
            id: row
            required property var modelData
            required property int index
            readonly property bool firstOfSection: index === 0 || view.rows[index - 1].section !== modelData.section
            Layout.fillWidth: true
            spacing: 3

            Label {
                visible: row.firstOfSection
                Layout.topMargin: row.index === 0 ? 4 : 14
                text: row.modelData.sectionTitle
                color: Theme.accent
                font.bold: true
                font.pixelSize: 15
                Accessible.role: Accessible.Heading
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight + 16
                radius: 6
                color: Theme.panel
                border.color: Theme.border
                opacity: row.modelData.available ? 1 : 0.75
                ColumnLayout {
                    id: card
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            color: Theme.text
                            font.bold: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: row.modelData.available ? (row.modelData.checked ? "✓ " + qsTr("available") : "· " + qsTr("not checked"))
                                                          : "✕ " + qsTr("unavailable")
                            color: row.modelData.available ? (row.modelData.checked ? Theme.accent : Theme.textDim) : "#e0806a"
                            font.pixelSize: 11
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.text
                        color: Theme.textDim
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: row.modelData.reason.length > 0
                        text: (row.modelData.available ? "" : qsTr("Why: ")) + row.modelData.reason
                        color: row.modelData.available ? Theme.textDim : "#e0806a"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: [qsTr("works offline"),
                               row.modelData.confirm === "ask" ? qsTr("asks you first")
                               : row.modelData.confirm === "model" ? qsTr("asks first when the wording was free")
                               : qsTr("no confirmation needed")].join(" · ")
                        color: Theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: row.modelData.examplesRu.concat(row.modelData.examplesEn)
                            delegate: DarkButton {
                                required property string modelData
                                text: "« " + modelData + " »"
                                font.pixelSize: 12
                                Accessible.name: qsTr("Put into the message field: %1").arg(modelData)
                                onClicked: view.exampleChosen(modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
