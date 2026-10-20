import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// One skill opened from its tile: calculators that work offline, formulas,
// ready answers (also offline) and the instructions Claude receives.
Popup {
    id: page
    required property var desktop
    required property var chat
    required property bool english
    property var skill: ({})
    signal askRequested(string question)

    function show(skillData) {
        skill = skillData
        sections.currentIndex = calculators.length > 0 ? 0 : formulas.length > 0 ? 1 : 2
        open()
    }
    function text(object, field) { return english && object[field + "_en"] ? object[field + "_en"] : (object[field] || "") }

    readonly property var calculators: skill.tools ? desktop.calculators(skill.tools) : []
    readonly property var formulas: skill.formulas || []
    readonly property var examples: skill.examples || []
    readonly property color tint: skill.color || Theme.accent

    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? Math.min(820, parent.width - 30) : 820
    height: parent ? Math.min(720, parent.height - 30) : 720
    padding: 0
    background: Rectangle { color: Theme.panel; radius: 22; border.color: Theme.border; clip: true }

    contentItem: ColumnLayout {
        spacing: 0
        // Banner: the skill's picture with its name over it.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            clip: true
            Rectangle { id: bannerBase; anchors.fill: parent; color: "#101b2b" }
            // The picture keeps its proportions on the right and fades into the banner.
            Image {
                id: banner
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: height * 2
                source: "qrc:/icons/skills/" + (page.skill.image || "default") + ".svg"
                sourceSize.width: 640
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                anchors.left: banner.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: banner.width * 0.55
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: bannerBase.color }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            Rectangle { anchors.fill: parent; color: Qt.rgba(page.tint.r, page.tint.g, page.tint.b, 0.08) }
            ColumnLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 20
                spacing: 2
                Label { text: page.text(page.skill, "name"); color: Theme.text; font.pixelSize: 26; font.bold: true; textFormat: Text.PlainText }
                Label { text: page.text(page.skill, "description"); color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
            }
            ToolButton {
                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                text: "×"
                onClicked: page.close()
                Accessible.name: qsTr("Close")
                contentItem: Text { text: "×"; color: Theme.text; font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 16; color: parent.hovered ? Theme.hover : Qt.rgba(0, 0, 0, 0.35) }
            }
        }

        TabBar {
            id: sections
            Layout.fillWidth: true
            Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 8
            background: null
            spacing: 4
            Repeater {
                model: [qsTr("Calculators") + " · " + page.calculators.length, qsTr("Formulas") + " · " + page.formulas.length,
                        qsTr("Answers") + " · " + page.examples.length, qsTr("Instructions")]
                TabButton {
                    id: sectionButton
                    required property string modelData
                    required property int index
                    text: modelData
                    enabled: index === 3 || !modelData.endsWith(" · 0")
                    contentItem: Text { text: sectionButton.text; color: sectionButton.checked ? page.tint : sectionButton.enabled ? Theme.textDim : Theme.border; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13; font.bold: sectionButton.checked }
                    background: Rectangle { color: sectionButton.checked ? Theme.selected : Theme.bg; radius: 8 }
                }
            }
        }

        StackLayout {
            currentIndex: sections.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16

            // ---------- Calculators ----------
            ListView {
                id: calcList
                clip: true
                spacing: 12
                model: page.calculators
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    property var values: ({})
                    property var result: ({ ok: false, lines: [] })
                    function recompute() { result = page.desktop.calculate(modelData.id, values) }
                    Component.onCompleted: {
                        const v = {}
                        for (const input of modelData.inputs) v[input.id] = input.value
                        values = v
                        recompute()
                    }
                    width: ListView.view.width
                    height: cardColumn.implicitHeight + 28
                    radius: 16
                    color: Theme.bg
                    border.color: Theme.border
                    ColumnLayout {
                        id: cardColumn
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: card.modelData.name; color: Theme.text; font.pixelSize: 17; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { text: card.modelData.formula; color: page.tint; font.family: "monospace"; font.pixelSize: 12; horizontalAlignment: Text.AlignRight; wrapMode: Text.Wrap; Layout.maximumWidth: card.width * 0.5 }
                        }
                        Label { text: card.modelData.description; color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 14
                            // Inputs: label, field, unit. Results update while typing.
                            ColumnLayout {
                                spacing: 6
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: Math.round((card.width - 42) * 0.56)
                                Layout.maximumWidth: Layout.preferredWidth
                                Repeater {
                                    model: card.modelData.inputs
                                    delegate: Item {
                                        id: inputRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: 34
                                        Label {
                                            id: inputLabel
                                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width * 0.48
                                            text: inputRow.modelData.label
                                            color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight
                                            ToolTip.visible: inputHover.hovered && truncated
                                            ToolTip.text: text
                                            HoverHandler { id: inputHover }
                                        }
                                        TextField {
                                            id: field
                                            anchors.left: inputLabel.right; anchors.leftMargin: 6
                                            anchors.right: unitLabel.left; anchors.rightMargin: 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            height: 32
                                            text: inputRow.modelData.value
                                            placeholderText: "—"
                                            selectByMouse: true
                                            color: Theme.text; placeholderTextColor: Theme.textDim; font.pixelSize: 13
                                            leftPadding: 8; rightPadding: 8; topPadding: 0; bottomPadding: 0
                                            background: Rectangle { color: Theme.panel; radius: 8; border.color: field.activeFocus ? page.tint : Theme.border }
                                            onTextEdited: {
                                                const v = Object.assign({}, card.values)
                                                v[inputRow.modelData.id] = text
                                                card.values = v
                                                card.recompute()
                                            }
                                        }
                                        Label {
                                            id: unitLabel
                                            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                            width: 58
                                            text: inputRow.modelData.unit
                                            color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                            // Results.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 180
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: resultColumn.implicitHeight + 20
                                radius: 12
                                color: Qt.rgba(page.tint.r, page.tint.g, page.tint.b, 0.08)
                                border.color: Qt.rgba(page.tint.r, page.tint.g, page.tint.b, 0.35)
                                ColumnLayout {
                                    id: resultColumn
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                                    spacing: 5
                                    Label {
                                        visible: !card.result.ok
                                        text: card.result.error || ""
                                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                                    }
                                    Repeater {
                                        model: card.result.ok ? card.result.lines : []
                                        delegate: ColumnLayout {
                                            required property var modelData
                                            spacing: 0
                                            Layout.fillWidth: true
                                            Label { text: modelData.label; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                            TextEdit { text: modelData.value; readOnly: true; selectByMouse: true; color: Theme.text; font.pixelSize: 15; font.bold: true; wrapMode: TextEdit.Wrap; Layout.fillWidth: true; selectionColor: page.tint }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Label { anchors.centerIn: parent; visible: calcList.count === 0; text: qsTr("This skill has no calculators."); color: Theme.textDim }
            }

            // ---------- Formulas ----------
            ListView {
                id: formulaList
                clip: true
                spacing: 8
                model: page.formulas
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: formulaCard
                    required property var modelData
                    width: ListView.view.width
                    height: formulaColumn.implicitHeight + 22
                    radius: 14
                    color: Theme.bg
                    border.color: Theme.border
                    ColumnLayout {
                        id: formulaColumn
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                        spacing: 4
                        Label { text: page.text(formulaCard.modelData, "title"); color: Theme.textDim; font.pixelSize: 12; textFormat: Text.PlainText }
                        TextEdit { text: formulaCard.modelData.formula; readOnly: true; selectByMouse: true; color: page.tint; font.pixelSize: 18; font.family: "serif"; wrapMode: TextEdit.Wrap; Layout.fillWidth: true; selectionColor: page.tint }
                        Label {
                            visible: text.length > 0
                            text: page.text(formulaCard.modelData, "note")
                            color: Theme.text; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText; opacity: 0.8
                        }
                    }
                }
            }

            // ---------- Answers ----------
            ListView {
                id: answerList
                clip: true
                spacing: 6
                model: page.examples
                ScrollBar.vertical: ScrollBar {}
                header: Label {
                    width: answerList.width
                    bottomPadding: 8
                    text: qsTr("Jarvis answers these questions instantly, even without the internet. Click one to read the answer.")
                    color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap
                }
                delegate: Rectangle {
                    id: answerCard
                    required property var modelData
                    property bool expanded: false
                    width: ListView.view.width
                    height: answerColumn.implicitHeight + 18
                    radius: 12
                    color: answerMouse.containsMouse && !expanded ? Theme.hover : Theme.bg
                    border.color: expanded ? Qt.darker(page.tint, 1.4) : Theme.border
                    MouseArea { id: answerMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: answerCard.expanded = !answerCard.expanded }
                    ColumnLayout {
                        id: answerColumn
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                        spacing: 8
                        Label { text: (answerCard.expanded ? "▾  " : "▸  ") + page.text(answerCard.modelData, "question"); color: Theme.text; font.pixelSize: 14; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                        TextEdit {
                            visible: answerCard.expanded
                            Layout.fillWidth: true
                            text: page.text(answerCard.modelData, "answer").replace(/</g, "&lt;")
                            textFormat: TextEdit.MarkdownText
                            readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                            color: Theme.text; font.pixelSize: 13; selectionColor: page.tint
                        }
                        DarkButton {
                            visible: answerCard.expanded
                            text: qsTr("Ask in chat")
                            onClicked: page.askRequested(page.text(answerCard.modelData, "question"))
                        }
                    }
                }
            }

            // ---------- Instructions ----------
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    Label { text: qsTr("What Claude is told when this skill is on:"); color: Theme.textDim; font.pixelSize: 12 }
                    TextEdit { text: page.skill.prompt || ""; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; textFormat: TextEdit.PlainText }
                }
            }
        }
    }
}
