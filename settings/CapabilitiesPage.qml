import QtQuick
import QtQuick.Layouts
import Jarvis

// Settings → What Jarvis can do (0.22.1): the same catalogue as the chat's
// "What Jarvis can do" button. An example goes into the main window's message
// field; nothing runs from here.
ColumnLayout {
    id: page
    required property var win
    spacing: 6

    AgentPanel { id: panel }

    CapabilitiesView {
        objectName: "entry:capabilities.menu"
        Layout.fillWidth: true
        Layout.leftMargin: 24
        Layout.rightMargin: 24
        Layout.bottomMargin: 24
        panel: panel
        english: page.win.english
        onExampleChosen: text => page.win.exampleChosen(text)
    }
}
