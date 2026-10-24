import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    property alias cfg_filterByScreen: byScreen.checked
    property alias cfg_showProfiles: profiles.checked
    property alias cfg_effects: effects.checked

    Kirigami.FormLayout {
        QQC2.CheckBox { id: byScreen; text: i18nd("jarvis-desktop", "Only the windows of this screen") }
        QQC2.CheckBox { id: profiles; text: i18nd("jarvis-desktop", "Workspaces and virtual desktops") }
        QQC2.CheckBox { id: effects; text: i18nd("jarvis-desktop", "Animations") }
        QQC2.Label {
            text: i18nd("jarvis-desktop", "Pin or unpin applications from their menu (right click). Position, size and auto-hide of the dock are the panel's: Jarvis → Control center → Desktop, or the panel's own settings.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            opacity: 0.7
        }
    }
}
