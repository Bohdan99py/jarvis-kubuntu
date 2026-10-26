import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Jarvis

// A row that opens a KDE System Settings module instead of a page of Jarvis's
// own. Shown only as working when the module is installed here; otherwise it
// says so, without a button that would do nothing.
SettingRow {
    id: row
    property string kcm
    property string description
    readonly property bool installed: kde.hasModule(kcm)
    KdeSettings { id: kde }
    hint: installed ? description : description + " " + qsTr("Not installed on this system.")

    DarkButton {
        visible: row.installed
        text: qsTr("Open in KDE")
        Accessible.name: qsTr("Open %1 in KDE System Settings").arg(row.title)
        onClicked: row.error = kde.openModule(row.kcm)
    }
}
