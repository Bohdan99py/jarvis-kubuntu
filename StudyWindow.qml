import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Jarvis

// French studies: lesson recordings with local transcription, translation of
// phrases, speech and PDFs into Russian, lesson notes and flashcards.
ApplicationWindow {
    id: win
    required property var study
    signal apiSettingsRequested()

    width: 1120
    height: 780
    minimumWidth: 900
    minimumHeight: 620
    title: qsTr("French studies — Jarvis")
    color: Theme.bg

    property string selectedText: ""
    property string selectedContext: ""
    property bool revealed: false
    property string pdfFile: ""

    function showTab(index) { tabs.currentIndex = index; show(); raise(); requestActivate() }
    function clock(seconds) {
        const s = Math.max(0, Math.floor(seconds))
        const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, r = s % 60
        const mm = (m < 10 ? "0" : "") + m, ss = (r < 10 ? "0" : "") + r
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }
    function delay(days) {
        if (days < 1 / 24) return qsTr("%1 min").arg(Math.max(1, Math.round(days * 24 * 60)))
        if (days < 1) return qsTr("%1 h").arg(Math.round(days * 24))
        if (days < 31) return qsTr("%1 d").arg(Math.round(days))
        if (days < 365) return qsTr("%1 mo").arg(Math.round(days / 30))
        return qsTr("%1 y").arg((days / 365).toFixed(1))
    }
    function currentName() {
        for (const r of study.recordings) if (r.path === study.current) return r.name
        return study.recording && study.current.length > 0 ? qsTr("Recording now") : ""
    }
    function currentRecording() {
        for (const r of study.recordings) if (r.path === study.current) return r
        return null
    }

    // ---------- Building blocks ----------
    component Panel: Rectangle { color: Theme.panel; radius: 18; border.color: Theme.border }
    component Title: Label { color: Theme.text; font.pixelSize: 17; font.bold: true }
    component Hint: Label { color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
    component DarkCheck: CheckBox {
        id: check
        indicator: Rectangle {
            x: check.leftPadding
            y: (check.height - height) / 2
            width: 20
            height: 20
            radius: 5
            color: Theme.bg
            border.color: check.checked && check.enabled ? Theme.accent : Theme.border
            Text { anchors.centerIn: parent; visible: check.checked; text: "✓"; color: check.enabled ? Theme.accent : Theme.textDim; font.pixelSize: 14; font.bold: true }
        }
        contentItem: Text { text: check.text; color: check.enabled ? Theme.text : Theme.textDim; leftPadding: check.indicator.width + 10; verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap; font.pixelSize: 13 }
    }
    component Field: TextField {
        color: Theme.text
        placeholderTextColor: Theme.textDim
        selectByMouse: true
        leftPadding: 12
        background: Rectangle { implicitHeight: 38; radius: 10; color: Theme.bg; border.color: parent.activeFocus ? Theme.accent : Theme.border }
    }
    component SmallButton: ToolButton {
        id: small
        implicitHeight: 28
        contentItem: Text { text: small.text; color: small.enabled ? Theme.text : Theme.textDim; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 14; color: small.hovered ? Theme.hover : Theme.botBubble }
    }
    component Meter: Rectangle {
        property real value: 0
        implicitHeight: 8
        radius: 4
        color: Theme.bg
        border.color: Theme.border
        Rectangle {
            width: Math.max(0, Math.min(1, parent.value)) * parent.width
            height: parent.height
            radius: 4
            color: parent.value > 0.85 ? Theme.danger : Theme.accent
            Behavior on width { NumberAnimation { duration: 120 } }
        }
    }
    component Badge: Rectangle {
        property alias text: badgeLabel.text
        property bool on: false
        implicitWidth: badgeLabel.implicitWidth + 12
        implicitHeight: 18
        radius: 9
        color: on ? Theme.selected : "transparent"
        border.color: on ? Theme.accent : Theme.border
        Label { id: badgeLabel; anchors.centerIn: parent; font.pixelSize: 10; color: parent.on ? Theme.accent : Theme.textDim }
    }

    FileDialog {
        id: audioDialog
        title: qsTr("Import a lesson recording")
        nameFilters: [qsTr("Audio and video (*.wav *.mp3 *.m4a *.ogg *.opus *.flac *.aac *.wma *.mp4 *.mkv *.webm *.mov)"), qsTr("All files (*)")]
        onAccepted: win.study.importAudio(selectedFile)
    }
    FileDialog {
        id: pdfDialog
        title: qsTr("Choose a French PDF")
        nameFilters: [qsTr("PDF documents (*.pdf)")]
        onAccepted: win.pdfFile = selectedFile
    }
    Dialog {
        id: confirmRemove
        property string path
        property string name
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Move the recording to the trash?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("%1 and its transcript, translation and notes go to the trash.").arg(confirmRemove.name); wrapMode: Text.Wrap; width: 360 }
        onAccepted: win.study.remove(path)
    }

    Shortcut { sequence: "Space"; enabled: tabs.currentIndex === 2 && win.study.card.id !== undefined && !win.revealed && !addFront.activeFocus && !addBack.activeFocus && !cardSearch.activeFocus; onActivated: win.revealed = true }
    Repeater {
        model: 4
        Item {
            required property int index
            Shortcut { sequence: String(index + 1); enabled: tabs.currentIndex === 2 && win.revealed && !addFront.activeFocus && !addBack.activeFocus && !cardSearch.activeFocus; onActivated: { win.study.review(win.study.card.id, index); win.revealed = false } }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // ---------- Header ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            ColumnLayout {
                spacing: 0
                Label { text: "FRANÇAIS"; color: Theme.accent; font.letterSpacing: 3; font.pixelSize: 11 }
                Label { text: qsTr("Studies"); color: Theme.text; font.pixelSize: 24; font.bold: true }
            }
            Item { Layout.fillWidth: true }
            TabBar {
                id: tabs
                objectName: "studyTabs"
                background: null
                spacing: 4
                Repeater {
                    model: [qsTr("Lessons"), qsTr("Translator"), qsTr("Flashcards") + (win.study.vocabStats.due > 0 ? " · " + win.study.vocabStats.due : ""), qsTr("Setup")]
                    TabButton {
                        id: tabButton
                        required property string modelData
                        text: modelData
                        width: implicitWidth + 24
                        contentItem: Text { text: tabButton.text; color: tabButton.checked ? Theme.accent : Theme.textDim; font.pixelSize: 14; font.bold: tabButton.checked; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: tabButton.checked ? Theme.selected : Theme.panel; radius: 10 }
                    }
                }
            }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ---------- Lessons ----------
            RowLayout {
                spacing: 14

                ColumnLayout {
                    Layout.preferredWidth: 320
                    Layout.maximumWidth: 320
                    Layout.fillHeight: true
                    spacing: 12

                    Panel {
                        Layout.fillWidth: true
                        implicitHeight: recordColumn.implicitHeight + 32
                        ColumnLayout {
                            id: recordColumn
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10
                            RowLayout {
                                spacing: 12
                                DarkButton {
                                    objectName: "recordButton"
                                    text: win.study.recording ? qsTr("■  Stop") : qsTr("●  Record")
                                    primary: !win.study.recording
                                    danger: win.study.recording
                                    enabled: !win.study.listening
                                    onClicked: win.study.recording ? win.study.stopRecording() : win.study.startRecording(liveCheck.checked && win.study.ready)
                                }
                                Label { text: win.clock(win.study.seconds); color: win.study.recording ? Theme.text : Theme.textDim; font.pixelSize: 22; font.family: "monospace" }
                            }
                            Meter { Layout.fillWidth: true; value: win.study.recording ? win.study.level : 0 }
                            DarkCheck {
                                id: liveCheck
                                Layout.fillWidth: true
                                text: qsTr("Transcribe while recording")
                                checked: true
                                enabled: win.study.ready && !win.study.recording
                            }
                            Hint {
                                visible: liveCheck.checked && win.study.ready
                                text: qsTr("The live text is a quick draft. After the lesson, Transcribe gives the accurate version.")
                            }
                            DarkCheck {
                                Layout.fillWidth: true
                                text: qsTr("Translate into Russian automatically")
                                checked: win.study.autoTranslate
                                enabled: win.study.claudeReady
                                onToggled: win.study.autoTranslate = checked
                            }
                            DarkCheck {
                                Layout.fillWidth: true
                                text: qsTr("Read translations aloud")
                                checked: win.study.speakTranslation
                                onToggled: win.study.speakTranslation = checked
                            }
                            Hint {
                                visible: !win.study.ready
                                text: qsTr("Recording works now. For French transcription install recognition on the Setup tab.")
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        DarkButton { text: qsTr("Import audio…"); Layout.fillWidth: true; enabled: !win.study.busy && win.study.ready; onClicked: audioDialog.open() }
                        DarkButton { text: qsTr("Folder"); onClicked: win.study.openFolder() }
                    }

                    ListView {
                        id: recordingList
                        objectName: "recordingList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: win.study.recordings
                        ScrollBar.vertical: ScrollBar {}
                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 20
                            visible: recordingList.count === 0
                            text: qsTr("No recordings yet. Press Record at the start of a lesson or import a file.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                        delegate: Rectangle {
                            id: recordingRow
                            required property var modelData
                            width: ListView.view.width
                            height: 58
                            radius: 12
                            color: modelData.path === win.study.current ? Theme.selected : (rowMouse.containsMouse ? Theme.hover : Theme.panel)
                            border.color: modelData.path === win.study.current ? Theme.accent : Theme.border
                            MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.study.open(recordingRow.modelData.path) }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: recordingRow.modelData.name; color: Theme.text; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                                    Label { text: win.clock(recordingRow.modelData.seconds); color: Theme.textDim; font.pixelSize: 12 }
                                }
                                Row {
                                    spacing: 6
                                    Badge { text: qsTr("FR text"); on: recordingRow.modelData.transcribed }
                                    Badge { text: qsTr("RU"); on: recordingRow.modelData.translated }
                                    Badge { text: qsTr("notes"); on: recordingRow.modelData.notes }
                                }
                            }
                        }
                    }
                }

                // Viewer
                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Title { text: win.currentName() || qsTr("Transcript"); Layout.fillWidth: true; elide: Text.ElideRight }
                            SmallButton { text: win.study.playing ? qsTr("■ Stop") : qsTr("▶ Listen"); visible: win.currentRecording() !== null; onClicked: win.study.playing ? win.study.stopPlayback() : win.study.play(0, 0) }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: win.currentRecording() !== null
                            DarkButton { text: qsTr("Transcribe"); enabled: !win.study.busy && win.study.ready; onClicked: win.study.transcribe(win.study.current) }
                            DarkButton { text: qsTr("Translate"); enabled: !win.study.busy && win.study.claudeReady && lineList.count > 0; onClicked: win.study.translateRecording(win.study.current) }
                            DarkButton { text: qsTr("Notes and words"); enabled: !win.study.busy && win.study.claudeReady && lineList.count > 0; onClicked: { win.study.makeNotes(win.study.current); viewTabs.currentIndex = 1 } }
                            DarkButton { text: qsTr("Open text"); enabled: lineList.count > 0; onClicked: win.study.openTranscript() }
                            DarkButton {
                                text: qsTr("Delete")
                                danger: true
                                onClicked: { confirmRemove.path = win.study.current; confirmRemove.name = win.currentName(); confirmRemove.open() }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TabBar {
                                id: viewTabs
                                background: null
                                spacing: 4
                                Repeater {
                                    model: [qsTr("Transcript"), qsTr("Notes")]
                                    TabButton {
                                        id: viewTab
                                        required property string modelData
                                        text: modelData
                                        width: implicitWidth + 20
                                        contentItem: Text { text: viewTab.text; color: viewTab.checked ? Theme.accent : Theme.textDim; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { color: viewTab.checked ? Theme.selected : Theme.bg; radius: 8 }
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            SmallButton {
                                text: qsTr("+ Flashcard: «%1»").arg(win.selectedText.length > 28 ? win.selectedText.substring(0, 26) + "…" : win.selectedText)
                                visible: win.selectedText.length > 0 && viewTabs.currentIndex === 0
                                onClicked: { win.study.addWord(win.selectedText, "", win.selectedContext); win.selectedText = "" }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Add the selected French words to your flashcards; Claude fills in the translation")
                            }
                        }
                        StackLayout {
                            currentIndex: viewTabs.currentIndex
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: lineList
                                objectName: "transcriptList"
                                clip: true
                                spacing: 10
                                model: win.study.lines
                                ScrollBar.vertical: ScrollBar {}
                                onCountChanged: if (win.study.liveRecording) Qt.callLater(lineList.positionViewAtEnd)
                                Label {
                                    anchors.centerIn: parent
                                    width: parent.width - 40
                                    visible: lineList.count === 0
                                    color: Theme.textDim
                                    wrapMode: Text.Wrap
                                    horizontalAlignment: Text.AlignHCenter
                                    text: win.study.liveRecording ? qsTr("Listening… French phrases appear here a few seconds after they are spoken.")
                                          : win.study.recording ? qsTr("Recording without live transcription. Transcribe it after you stop.")
                                          : win.currentRecording() === null ? qsTr("Choose a recording on the left.")
                                          : win.currentRecording().transcribed ? qsTr("The transcript is empty: no French speech was recognized.")
                                          : qsTr("Not transcribed yet. Press Transcribe — recognition runs on this computer.")
                                }
                                delegate: RowLayout {
                                    id: line
                                    required property int index
                                    required property real start
                                    required property real end
                                    required property string fr
                                    required property string ru
                                    width: ListView.view.width - 12
                                    spacing: 10
                                    SmallButton {
                                        Layout.alignment: Qt.AlignTop
                                        text: win.clock(line.start)
                                        enabled: !win.study.liveRecording
                                        onClicked: win.study.play(line.start, line.end + 0.3)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Listen to this phrase")
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        TextEdit {
                                            Layout.fillWidth: true
                                            text: line.fr
                                            readOnly: true
                                            selectByMouse: true
                                            wrapMode: TextEdit.Wrap
                                            color: Theme.text
                                            selectionColor: Theme.userBubble
                                            font.pixelSize: Theme.fontSize
                                            textFormat: TextEdit.PlainText
                                            onSelectedTextChanged: if (selectedText.trim().length > 0) { win.selectedText = selectedText.trim(); win.selectedContext = line.fr }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            text: line.ru
                                            color: Theme.accent
                                            wrapMode: Text.Wrap
                                            font.pixelSize: 14
                                            textFormat: Text.PlainText
                                        }
                                    }
                                }
                            }

                            ScrollView {
                                id: notesScroll
                                clip: true
                                contentWidth: availableWidth
                                TextArea {
                                    width: notesScroll.availableWidth
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    color: Theme.text
                                    textFormat: TextEdit.MarkdownText
                                    text: win.study.notes.length > 0 ? win.study.notes
                                          : (win.study.job === "notes" ? qsTr("Claude is writing the notes…")
                                                                        : qsTr("No notes yet. \"Notes and words\" asks Claude for a Russian summary of the lesson — topic, grammar, useful phrases, homework — and adds the key words to your flashcards."))
                                    background: null
                                }
                            }
                        }
                    }
                }
            }

            // ---------- Translator ----------
            ScrollView {
                id: translatorScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: translatorScroll.availableWidth
                    spacing: 14

                    Panel {
                        Layout.fillWidth: true
                        implicitHeight: phraseColumn.implicitHeight + 36
                        ColumnLayout {
                            id: phraseColumn
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10
                            Title { text: qsTr("Phrase or speech") }
                            Hint { text: qsTr("Type French (or Russian — it is translated into French), or press the microphone and speak French: the phrase is recognized here and translated by Claude.") }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 96
                                radius: 12
                                color: Theme.bg
                                border.color: phraseInput.activeFocus ? Theme.accent : Theme.border
                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    TextArea {
                                        id: phraseInput
                                        objectName: "phraseInput"
                                        wrapMode: TextEdit.Wrap
                                        color: Theme.text
                                        placeholderText: qsTr("Bonjour, je voudrais…")
                                        placeholderTextColor: Theme.textDim
                                        selectByMouse: true
                                        background: null
                                        Keys.onReturnPressed: event => { if (event.modifiers & Qt.ControlModifier) win.study.translatePhrase(text); else event.accepted = false }
                                    }
                                }
                            }
                            RowLayout {
                                spacing: 8
                                DarkButton { text: qsTr("Translate"); primary: true; enabled: phraseInput.text.trim().length > 0 && win.study.claudeReady && !win.study.phraseBusy; onClicked: win.study.translatePhrase(phraseInput.text) }
                                DarkButton {
                                    text: win.study.listening ? qsTr("■  Stop") : qsTr("🎤  Speak French")
                                    danger: win.study.listening
                                    enabled: win.study.ready && !win.study.recording
                                    onClicked: win.study.listening ? win.study.stopPhrase() : win.study.listenPhrase()
                                }
                                Meter { Layout.preferredWidth: 120; visible: win.study.listening; value: win.study.level }
                                SmallButton { text: qsTr("🔊 French"); enabled: phraseInput.text.trim().length > 0; onClicked: win.study.speak(phraseInput.text, "fr") }
                            }
                            Hint { visible: !win.study.ready; text: qsTr("Speech input needs French recognition (Setup tab).") }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; visible: win.study.phrase.length > 0 || win.study.phraseTranslation.length > 0 }
                            Label { visible: win.study.phrase.length > 0; text: win.study.phrase; color: Theme.textDim; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText; font.italic: true }
                            TextEdit {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: win.study.phraseTranslation
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                color: win.study.phraseBusy ? Theme.textDim : Theme.accent
                                font.pixelSize: 18
                                textFormat: TextEdit.PlainText
                            }
                            RowLayout {
                                visible: win.study.phraseTranslation.length > 0 && !win.study.phraseBusy
                                spacing: 8
                                SmallButton { text: qsTr("🔊 Listen"); onClicked: win.study.speak(win.study.phraseTranslation, /[а-яё]/i.test(win.study.phraseTranslation) ? "ru" : "fr") }
                                SmallButton { text: qsTr("Edit the recognized phrase"); visible: win.study.phrase.length > 0 && phraseInput.text !== win.study.phrase; onClicked: phraseInput.text = win.study.phrase }
                                SmallButton {
                                    text: qsTr("+ Flashcard")
                                    visible: win.study.phrase.length > 0 && win.study.phrase.length <= 300
                                    onClicked: {
                                        const russianFirst = /[а-яё]/i.test(win.study.phrase)
                                        win.study.addWord(russianFirst ? win.study.phraseTranslation : win.study.phrase,
                                                          russianFirst ? win.study.phrase : win.study.phraseTranslation, "")
                                    }
                                }
                            }
                            Label { visible: win.study.vocabStatus.length > 0 && tabs.currentIndex === 1; text: win.study.vocabStatus; color: Theme.textDim; font.pixelSize: 12 }
                        }
                    }

                    Panel {
                        Layout.fillWidth: true
                        implicitHeight: pdfColumn.implicitHeight + 36
                        ColumnLayout {
                            id: pdfColumn
                            anchors.fill: parent
                            anchors.margins: 18
                            spacing: 10
                            Title { text: qsTr("PDF document") }
                            Hint { text: qsTr("The text is taken from the PDF (scanned pages are recognized with Tesseract if tesseract-ocr-fra is installed), translated by Claude paragraph by paragraph and saved as a new PDF and a text file in Documents/Jarvis/French/pdf.") }
                            RowLayout {
                                spacing: 10
                                DarkButton { text: qsTr("Choose PDF…"); enabled: !win.study.busy; onClicked: pdfDialog.open() }
                                Label {
                                    Layout.fillWidth: true
                                    text: win.pdfFile.length > 0 ? decodeURIComponent(win.pdfFile.toString().replace(/^.*\//, "")) : qsTr("No file chosen")
                                    color: win.pdfFile.length > 0 ? Theme.text : Theme.textDim
                                    elide: Text.ElideMiddle
                                }
                            }
                            DarkCheck { id: bilingualCheck; text: qsTr("Bilingual: each French paragraph followed by its translation"); checked: true }
                            RowLayout {
                                spacing: 10
                                DarkButton {
                                    text: qsTr("Translate PDF")
                                    primary: true
                                    enabled: win.pdfFile.length > 0 && !win.study.busy && win.study.claudeReady
                                    onClicked: win.study.translatePdf(win.pdfFile, bilingualCheck.checked)
                                }
                                DarkButton { text: qsTr("Open result"); visible: win.study.pdfResult.length > 0; onClicked: win.study.openFile(win.study.pdfResult) }
                                DarkButton { text: qsTr("Folder"); onClicked: win.study.openFolder() }
                            }
                            Hint { visible: !win.study.claudeReady; text: qsTr("Translation needs a Claude API key.") }
                        }
                    }
                }
            }

            // ---------- Flashcards ----------
            RowLayout {
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12
                    Row {
                        spacing: 10
                        Repeater {
                            model: [
                                { label: qsTr("To review"), value: win.study.vocabStats.due || 0 },
                                { label: qsTr("Reviewed today"), value: win.study.vocabStats.reviewedToday || 0 },
                                { label: qsTr("Learned"), value: win.study.vocabStats.learned || 0 },
                                { label: qsTr("All cards"), value: win.study.vocabStats.total || 0 }
                            ]
                            Panel {
                                required property var modelData
                                width: 128
                                height: 64
                                Column {
                                    anchors.centerIn: parent
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.value; color: Theme.text; font.pixelSize: 22; font.bold: true }
                                    Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; color: Theme.textDim; font.pixelSize: 11 }
                                }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ColumnLayout {
                            anchors.centerIn: parent
                            width: parent.width - 60
                            spacing: 14
                            visible: win.study.card.id === undefined
                            Label { Layout.alignment: Qt.AlignHCenter; text: "✓"; color: Theme.accent; font.pixelSize: 48 }
                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                color: Theme.text
                                font.pixelSize: 17
                                text: (win.study.vocabStats.total || 0) === 0
                                      ? qsTr("No flashcards yet. Add words on the right, select words in a transcript, or let \"Notes and words\" pick them from a lesson.")
                                      : qsTr("Nothing to review right now. Come back later — words return just before you would forget them.")
                            }
                        }
                        ColumnLayout {
                            anchors.centerIn: parent
                            width: parent.width - 60
                            spacing: 16
                            visible: win.study.card.id !== undefined
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 10
                                Label { text: win.study.card.front || ""; color: Theme.text; font.pixelSize: 32; font.bold: true; wrapMode: Text.Wrap; Layout.maximumWidth: 520; horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText }
                                SmallButton { text: "🔊"; onClicked: win.study.speak(win.study.card.front, "fr"); Accessible.name: qsTr("Pronounce") }
                            }
                            DarkButton {
                                Layout.alignment: Qt.AlignHCenter
                                visible: !win.revealed
                                text: qsTr("Show the answer  ·  Space")
                                primary: true
                                onClicked: win.revealed = true
                            }
                            Label { Layout.fillWidth: true; visible: win.revealed; text: win.study.card.back || ""; color: Theme.accent; font.pixelSize: 22; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText }
                            Label { Layout.fillWidth: true; visible: win.revealed && (win.study.card.example || "").length > 0; text: win.study.card.example || ""; color: Theme.textDim; font.pixelSize: 15; font.italic: true; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; textFormat: Text.PlainText }
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                visible: win.revealed
                                spacing: 10
                                Repeater {
                                    model: [qsTr("Again"), qsTr("Hard"), qsTr("Good"), qsTr("Easy")]
                                    DarkButton {
                                        required property string modelData
                                        required property int index
                                        text: modelData + "  ·  " + win.delay((win.study.card.plan || [0, 0, 0, 0])[index])
                                        primary: index === 2
                                        danger: index === 0
                                        onClicked: { win.study.review(win.study.card.id, index); win.revealed = false }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Key %1").arg(index + 1)
                                    }
                                }
                            }
                        }
                    }
                }

                Panel {
                    Layout.preferredWidth: 380
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10
                        Title { text: qsTr("Add a word") }
                        Field { id: addFront; objectName: "addFront"; Layout.fillWidth: true; placeholderText: qsTr("French: la bibliothèque"); onAccepted: addBack.forceActiveFocus() }
                        Field { id: addBack; Layout.fillWidth: true; placeholderText: win.study.claudeReady ? qsTr("Russian (empty: Claude translates)") : qsTr("Russian translation"); onAccepted: addButton.clicked() }
                        DarkButton {
                            id: addButton
                            text: qsTr("Add")
                            primary: true
                            enabled: addFront.text.trim().length > 0
                            onClicked: { win.study.addWord(addFront.text, addBack.text, ""); addFront.text = ""; addBack.text = ""; addFront.forceActiveFocus() }
                        }
                        Label { visible: win.study.vocabStatus.length > 0; text: win.study.vocabStatus; color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                        Field { id: cardSearch; Layout.fillWidth: true; placeholderText: qsTr("Search the deck…") }
                        ListView {
                            id: cardList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            ScrollBar.vertical: ScrollBar {}
                            model: {
                                const q = cardSearch.text.trim().toLowerCase()
                                const all = win.study.cards
                                return q.length === 0 ? all : all.filter(c => (c.front + " " + c.back).toLowerCase().indexOf(q) >= 0)
                            }
                            delegate: Rectangle {
                                id: cardRow
                                required property var modelData
                                width: ListView.view.width - 10
                                height: 46
                                radius: 10
                                color: cardMouse.containsMouse ? Theme.hover : Theme.bg
                                MouseArea { id: cardMouse; anchors.fill: parent; hoverEnabled: true; onClicked: win.study.speak(cardRow.modelData.front, "fr") }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 4
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Label { text: cardRow.modelData.front; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                                        Label { text: cardRow.modelData.back; color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true; textFormat: Text.PlainText }
                                    }
                                    SmallButton { text: "×"; onClicked: win.study.removeWord(cardRow.modelData.id); Accessible.name: qsTr("Delete card") }
                                }
                            }
                        }
                    }
                }
            }

            // ---------- Setup ----------
            ScrollView {
                id: setupScroll
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: Math.min(760, setupScroll.availableWidth)
                    spacing: 14
                    Title { text: win.study.ready ? qsTr("French recognition is ready") : qsTr("French speech recognition") }
                    Hint { font.pixelSize: 13; text: qsTr("Jarvis recognizes French with Whisper (faster-whisper) on this computer: lesson audio is never uploaded. Setup downloads the program from PyPI (about 450 MB) and the chosen model from Hugging Face into ~/.local/share/jarvis/study.") }
                    RowLayout {
                        spacing: 12
                        Label { text: qsTr("Model"); color: Theme.text }
                        ComboBox {
                            id: modelCombo
                            Layout.preferredWidth: 380
                            model: [qsTr("Fast — base, ~145 MB, rougher"), qsTr("Balanced — small, ~480 MB (recommended)"), qsTr("Accurate — medium, ~1.5 GB, slow on CPU")]
                            currentIndex: ["base", "small", "medium"].indexOf(win.study.model)
                            enabled: !win.study.busy
                            onActivated: index => win.study.model = ["base", "small", "medium"][index]
                            contentItem: Text { leftPadding: 12; text: modelCombo.displayText; color: Theme.text; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                            background: Rectangle { implicitHeight: 38; radius: 10; color: Theme.panel; border.color: modelCombo.activeFocus ? Theme.accent : Theme.border }
                        }
                    }
                    DarkButton {
                        objectName: "studySetupButton"
                        text: win.study.ready ? qsTr("Check the installation") : qsTr("Install recognition")
                        primary: !win.study.ready
                        enabled: !win.study.busy
                        onClicked: win.study.setup()
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Title { text: qsTr("Translation and notes") }
                    Hint {
                        font.pixelSize: 13
                        text: win.study.claudeReady
                              ? qsTr("Claude is connected. Transcripts, phrases and PDF text (never audio) are sent to Anthropic for translation and notes.")
                              : qsTr("Add a Claude API key to translate into Russian and to get lesson notes. Transcription and flashcards work without it.")
                    }
                    DarkButton { text: qsTr("Claude API settings"); onClicked: win.apiSettingsRequested() }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                    Title { text: qsTr("Where things are") }
                    Hint { font.pixelSize: 13; text: qsTr("Recordings, transcripts (.fr.txt), translations (.ru.txt), notes (.notes.md) and translated PDFs: %1. Flashcards: ~/.local/share/jarvis/vocabulary.json. Jarvis remembers that you study French and can quiz you in chat: \"quiz me on my French words\".").arg(win.study.folder) }
                    DarkButton { text: qsTr("Open the folder"); onClicked: win.study.openFolder() }
                }
            }
        }

        // ---------- Status ----------
        RowLayout {
            Layout.fillWidth: true
            visible: win.study.status.length > 0 || win.study.busy
            spacing: 12
            BusyIndicator { visible: win.study.busy && win.study.progress < 0; running: visible; Layout.preferredWidth: 26; Layout.preferredHeight: 26 }
            ProgressBar { visible: win.study.busy && win.study.progress >= 0; value: win.study.progress; Layout.preferredWidth: 160 }
            Label { text: win.study.status; color: Theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true; textFormat: Text.PlainText; maximumLineCount: 3; elide: Text.ElideRight }
            DarkButton { text: qsTr("Cancel"); visible: win.study.busy; onClicked: win.study.cancel() }
        }
    }

    Connections {
        target: win.study
        function onReviewChanged() { if (win.study.card.id === undefined) win.revealed = false }
    }
}
