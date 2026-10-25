import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A question before an action, with Jarvis's own buttons: the dialog's standard
// buttons come from the platform theme in the system's language, which mixes
// languages when Jarvis speaks another one. The keyboard focus starts on the
// safe choice (Cancel); Shift+Tab or a click reaches the action.
Dialog {
    id: dlg
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool destructive: false
    // Only an acknowledgement (one button).
    property bool notice: false

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 40 : 520)
    modal: true
    onOpened: (notice ? acceptButton : rejectButton).forceActiveFocus(Qt.TabFocusReason)
    footer: RowLayout {
        spacing: 8
        Item { Layout.fillWidth: true }
        DarkButton {
            id: acceptButton
            objectName: "dialogAccept"
            Layout.bottomMargin: 14
            Layout.rightMargin: dlg.notice ? 14 : 0
            text: dlg.acceptText
            primary: !dlg.destructive
            danger: dlg.destructive
            onClicked: dlg.accept()
        }
        DarkButton {
            id: rejectButton
            objectName: "dialogReject"
            visible: !dlg.notice
            Layout.bottomMargin: 14
            Layout.rightMargin: 14
            text: dlg.rejectText
            onClicked: dlg.reject()
        }
    }
}
