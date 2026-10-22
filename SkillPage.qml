import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
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
    // A question with files for the chat (a KiCad schematic to look at).
    signal askWithFilesRequested(string question, var files)

    function show(skillData) {
        skill = skillData
        const first = calculators.length > 0 ? "calculators" : formulas.length > 0 ? "formulas" : circuits.length > 0 ? "schematics" : "answers"
        sections.currentIndex = Math.max(0, tabs.findIndex(t => t.key === first))
        open()
    }
    function showTab(key) { sections.currentIndex = Math.max(0, tabs.findIndex(t => t.key === key)) }
    function text(object, field) { return english && object[field + "_en"] ? object[field + "_en"] : (object[field] || "") }

    readonly property var calculators: skill.tools ? desktop.calculators(skill.tools) : []
    readonly property var formulas: skill.formulas || []
    readonly property var examples: skill.examples || []
    readonly property color tint: skill.color || Theme.accent
    // Example circuits the skill brings, and the KiCad workbench that comes with them.
    readonly property var circuits: skill.schematics ? kicad.examples.filter(e => skill.schematics.indexOf(e.id) >= 0) : []
    readonly property var tabs: [
        { key: "calculators", title: qsTr("Calculators") + " · " + calculators.length, enabled: calculators.length > 0 },
        { key: "formulas", title: qsTr("Formulas") + " · " + formulas.length, enabled: formulas.length > 0 },
        { key: "schematics", title: qsTr("Schematics") + " · " + circuits.length, enabled: true },
        { key: "kicad", title: "KiCad", enabled: true },
        { key: "answers", title: qsTr("Answers") + " · " + examples.length, enabled: examples.length > 0 },
        { key: "instructions", title: qsTr("Instructions"), enabled: true }
    ].filter(t => (t.key !== "schematics" && t.key !== "kicad") || circuits.length > 0)
    readonly property var stackIndex: ({ calculators: 0, formulas: 1, answers: 2, instructions: 3, schematics: 4, kicad: 5 })

    KicadController { id: kicad; english: page.english }

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
                model: page.tabs
                TabButton {
                    id: sectionButton
                    required property var modelData
                    text: modelData.title
                    enabled: modelData.enabled
                    contentItem: Text { text: sectionButton.text; color: sectionButton.checked ? page.tint : sectionButton.enabled ? Theme.textDim : Theme.border; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 13; font.bold: sectionButton.checked }
                    background: Rectangle { color: sectionButton.checked ? Theme.selected : Theme.bg; radius: 8 }
                }
            }
        }

        StackLayout {
            currentIndex: page.stackIndex[(page.tabs[sections.currentIndex] || page.tabs[0]).key]
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
                            text: page.desktop.safeMarkdown(page.text(answerCard.modelData, "answer"))
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

            // ---------- Schematics: the example circuits ----------
            ColumnLayout {
                spacing: 8
                Label {
                    text: qsTr("Classic circuits with values that work. Open one to zoom in, ask Jarvis how it works, or save it: SPICE for ngspice, a netlist for the KiCad PCB editor, a bill of materials.")
                    color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
                GridView {
                    id: circuitGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: page.circuits
                    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 250)))
                    cellHeight: 200
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        id: circuitCell
                        required property var modelData
                        width: circuitGrid.cellWidth
                        height: circuitGrid.cellHeight
                        Rectangle {
                            anchors.fill: parent; anchors.margins: 5
                            radius: 14
                            color: circuitMouse.containsMouse ? Theme.hover : Theme.bg
                            border.color: circuitMouse.containsMouse ? page.tint : Theme.border
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 8
                                spacing: 4
                                Image {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    source: circuitCell.modelData.image
                                    sourceSize.width: 480
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }
                                Label { text: circuitCell.modelData.title; color: Theme.text; font.pixelSize: 13; font.bold: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                            }
                            MouseArea { id: circuitMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: circuitView.show(circuitCell.modelData) }
                        }
                    }
                }
                Label { text: kicad.status; visible: text.length > 0; color: page.tint; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
            }

            // ---------- KiCad: the user's projects ----------
            // Loaded when shown: asking kicad-cli for its version takes a moment.
            Loader {
                active: (page.tabs[sections.currentIndex] || {}).key === "kicad"
                sourceComponent: kicadPage
            }
        }
    }

    // Shows a sheet or a board: Ctrl+wheel or the buttons zoom, drag to move.
    component ZoomView: Item {
        id: zoomView
        property url source
        property bool paper: false
        property real zoom: 1
        clip: true
        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: Math.max(width, width * zoomView.zoom)
            contentHeight: Math.max(height, height * zoomView.zoom)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
            Rectangle {
                width: flick.contentWidth; height: flick.contentHeight
                color: zoomView.paper ? "#ffffff" : "transparent"
                radius: 8
                Image {
                    anchors.fill: parent; anchors.margins: 6
                    source: zoomView.source
                    sourceSize: Qt.size(width, height)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    smooth: true
                }
            }
            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                onWheel: event => zoomView.zoom = Math.max(1, Math.min(8, zoomView.zoom * (event.angleDelta.y > 0 ? 1.2 : 1 / 1.2)))
            }
        }
        Row {
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
            spacing: 4
            Repeater {
                model: [["−", 1 / 1.4], ["⤢", 0], ["+", 1.4]]
                delegate: ToolButton {
                    required property var modelData
                    text: modelData[0]
                    Accessible.name: modelData[1] === 0 ? qsTr("Fit") : modelData[1] > 1 ? qsTr("Zoom in") : qsTr("Zoom out")
                    onClicked: zoomView.zoom = modelData[1] === 0 ? 1 : Math.max(1, Math.min(8, zoomView.zoom * modelData[1]))
                    contentItem: Text { text: parent.text; color: Theme.text; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { implicitWidth: 32; implicitHeight: 32; radius: 8; color: parent.hovered ? Theme.hover : Qt.rgba(0, 0, 0, 0.45) }
                }
            }
        }
    }

    // One example circuit: the drawing, how it works, parts and nets, files.
    Popup {
        id: circuitView
        property var circuit: ({})
        function show(c) { circuit = c; circuitZoom.zoom = 1; open() }
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent ? Math.min(900, parent.width - 20) : 900
        height: parent ? Math.min(760, parent.height - 20) : 760
        padding: 16
        background: Rectangle { color: Theme.panel; radius: 20; border.color: page.tint }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label { text: circuitView.circuit.title || ""; color: Theme.text; font.pixelSize: 20; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                DarkButton { text: qsTr("Close"); onClicked: circuitView.close() }
            }
            ZoomView {
                id: circuitZoom
                Layout.fillWidth: true
                Layout.preferredHeight: 330
                source: circuitView.circuit.image || ""
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    Label { text: circuitView.circuit.description || ""; color: Theme.text; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: circuitView.circuit.formulas || []
                            delegate: Rectangle {
                                required property string modelData
                                width: formulaText.implicitWidth + 18; height: 28; radius: 8
                                color: Qt.rgba(page.tint.r, page.tint.g, page.tint.b, 0.12)
                                Label { id: formulaText; anchors.centerIn: parent; text: parent.modelData; color: page.tint; font.family: "serif"; font.pixelSize: 14 }
                            }
                        }
                    }
                    Label { text: qsTr("Parts and nets — what Jarvis reads from a schematic:"); color: Theme.textDim; font.pixelSize: 12 }
                    TextEdit {
                        text: circuitView.circuit.id ? kicad.exampleDigest(circuitView.circuit.id) : ""
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        color: Theme.text; font.family: "monospace"; font.pixelSize: 12; Layout.fillWidth: true; selectionColor: page.tint
                        textFormat: TextEdit.PlainText
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                DarkButton { text: qsTr("Ask Jarvis about it"); primary: true; onClicked: { circuitView.close(); page.askRequested(kicad.exampleQuestion(circuitView.circuit.id)) } }
                DarkButton { text: qsTr("SPICE (.cir)"); visible: (circuitView.circuit.spice || "").length > 0; onClicked: kicad.saveExample(circuitView.circuit.id, "cir") }
                DarkButton { text: qsTr("Netlist for KiCad (.net)"); onClicked: kicad.saveExample(circuitView.circuit.id, "net") }
                DarkButton { text: qsTr("Parts list (.csv)"); onClicked: kicad.saveExample(circuitView.circuit.id, "csv") }
                DarkButton { text: qsTr("Drawing (.svg)"); onClicked: kicad.saveExample(circuitView.circuit.id, "svg") }
            }
            Label { text: kicad.status; visible: text.length > 0; color: page.tint; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
        }
    }

    FileDialog {
        id: kicadFile
        title: qsTr("Open a KiCad project")
        nameFilters: [qsTr("KiCad (*.kicad_pro *.kicad_sch *.kicad_pcb)")]
        onAccepted: kicad.choose(selectedFile)
    }

    Component {
        id: kicadPage
        RowLayout {
            spacing: 12
            Component.onCompleted: if (kicad.projects.length === 0) kicad.refreshProjects()
            // Projects: the user's (newest first) and KiCad's demos.
            ColumnLayout {
                Layout.preferredWidth: 220
                Layout.maximumWidth: 220
                Layout.fillHeight: true
                spacing: 6
                DarkButton { text: qsTr("Open a project…"); primary: true; Layout.fillWidth: true; onClicked: kicadFile.open() }
                Label { text: qsTr("Projects and KiCad demos"); color: Theme.textDim; font.pixelSize: 12 }
                ListView {
                    id: projectList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: kicad.projects
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: projectRow
                        required property var modelData
                        width: ListView.view.width
                        height: projectColumn.implicitHeight + 12
                        radius: 8
                        color: kicad.file.indexOf(modelData.folder) >= 0 && kicad.name === modelData.name ? Theme.selected : projectMouse.containsMouse ? Theme.hover : Theme.bg
                        ColumnLayout {
                            id: projectColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 8
                            spacing: 0
                            Label { text: (projectRow.modelData.demo ? "◇ " : "") + projectRow.modelData.name; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                            Label { text: projectRow.modelData.demo ? qsTr("KiCad demo") : projectRow.modelData.folder; color: Theme.textDim; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true; textFormat: Text.PlainText }
                        }
                        MouseArea { id: projectMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: kicad.chooseProject(projectRow.modelData.path) }
                    }
                    Label { anchors.centerIn: parent; width: parent.width; visible: projectList.count === 0; text: qsTr("No KiCad projects found in Documents, Projects or KiCad folders."); color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Label {
                    text: kicad.cliAvailable
                          ? (kicad.name.length > 0 ? kicad.name : qsTr("Open a KiCad project, or pick one on the left. Jarvis only reads it: previews, checks and exports go to Documents/Jarvis/KiCad."))
                          : qsTr("KiCad is not installed. Install it (sudo apt install kicad, version 8 or newer recommended) to preview, check and export projects. Without it Jarvis still makes a parts list from a schematic.")
                    color: kicad.name.length > 0 ? Theme.text : Theme.textDim
                    font.pixelSize: kicad.name.length > 0 ? 17 : 12; font.bold: kicad.name.length > 0
                    wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: kicad.file.length > 0
                    DarkButton { text: qsTr("Schematic"); enabled: kicad.hasSchematic && !kicad.busy; onClicked: kicad.preview(false) }
                    DarkButton { text: qsTr("Board"); enabled: kicad.hasBoard && !kicad.busy; onClicked: kicad.preview(true) }
                    DarkButton { text: "ERC"; enabled: kicad.hasSchematic && !kicad.busy; onClicked: kicad.runErc() }
                    DarkButton { text: "DRC"; enabled: kicad.hasBoard && !kicad.busy; onClicked: kicad.runDrc() }
                    DarkButton { text: qsTr("Parts list"); enabled: kicad.hasSchematic && !kicad.busy; onClicked: kicad.exportBom() }
                    DarkButton { text: qsTr("Netlist"); enabled: kicad.hasSchematic && !kicad.busy && kicad.cliAvailable; onClicked: kicad.exportNetlist() }
                    DarkButton { text: "PDF"; enabled: kicad.hasSchematic && !kicad.busy && kicad.cliAvailable; onClicked: kicad.exportPdf() }
                    DarkButton { text: qsTr("Gerber files"); enabled: kicad.hasBoard && !kicad.busy && kicad.cliAvailable; onClicked: kicad.exportFabrication() }
                    DarkButton { text: qsTr("Results folder"); onClicked: kicad.openOutputFolder() }
                    DarkButton { text: qsTr("Open in KiCad"); enabled: kicad.kicadAvailable; onClicked: kicad.openInKicad() }
                    DarkButton {
                        text: qsTr("Ask Jarvis about it")
                        primary: true
                        onClicked: page.askWithFilesRequested(kicad.reportQuestion().length > 0 ? kicad.reportQuestion()
                                                              : qsTr("Check this circuit: what it does, mistakes in the connections, missing pull-ups, decoupling or protection, and what to improve."),
                                                              [kicad.attachment()])
                    }
                }
                Label { text: kicad.status; visible: text.length > 0; color: page.tint; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                BusyIndicator { visible: kicad.busy; running: visible; Layout.preferredHeight: 28; Layout.preferredWidth: 28 }
                // Preview pages.
                RowLayout {
                    visible: kicad.pages.length > 1
                    spacing: 4
                    Repeater {
                        model: kicad.pages
                        delegate: DarkButton {
                            required property var modelData
                            required property int index
                            text: modelData.title
                            primary: pageZoom.page === index
                            onClicked: { pageZoom.page = index; pageZoom.zoom = 1 }
                        }
                    }
                }
                ZoomView {
                    id: pageZoom
                    property int page: 0
                    visible: kicad.pages.length > 0
                    paper: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 200
                    source: kicad.pages.length > 0 ? kicad.pages[Math.min(page, kicad.pages.length - 1)].url : ""
                    Connections { target: kicad; function onChanged() { if (pageZoom.page >= kicad.pages.length) pageZoom.page = 0 } }
                }
                // ERC/DRC findings.
                Label { text: kicad.reportTitle; visible: text.length > 0; color: Theme.text; font.bold: true }
                ListView {
                    visible: kicad.report.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(160, contentHeight)
                    clip: true
                    spacing: 3
                    model: kicad.report
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Label {
                        required property var modelData
                        width: ListView.view.width
                        text: (modelData.severity === "error" ? "⛔ " : modelData.severity === "warning" ? "⚠ " : "• ") + modelData.description + (modelData.where.length > 0 ? " — " + modelData.where : "")
                        color: modelData.severity === "error" ? Theme.danger : Theme.text
                        font.pixelSize: 12; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
