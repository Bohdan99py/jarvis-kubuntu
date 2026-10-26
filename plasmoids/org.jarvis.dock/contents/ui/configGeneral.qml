import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import org.kde.plasma.plasmoid

// The dock's own settings (they apply at once and are kept in Plasma's
// layout). Where the panel is — edge, thickness, alignment, auto-hide — is
// the panel's: "Panel settings…" opens Plasma's own editor; Jarvis's Control
// center sets the same for the JARVIS DESKTOP panel.
KCM.SimpleKCM {
    property alias cfg_filterByScreen: byScreen.checked
    property alias cfg_groupWindows: grouping.checked
    property alias cfg_showProfiles: profiles.checked
    property alias cfg_showDesktops: desktops.checked
    property alias cfg_showJarvis: jarvis.checked
    property alias cfg_panelStyle: panelStyle.checked
    property alias cfg_iconSize: iconSize.value
    property alias cfg_spacing: gapSlider.value
    property alias cfg_padding: paddingSlider.value
    property alias cfg_surfaceOpacity: glassSlider.value
    property alias cfg_glow: glow.value
    property alias cfg_effects: effects.checked
    property alias cfg_magnification: magnification.checked
    property alias cfg_magnifyAmount: magnifyAmount.value
    property var cfg_launchers: []
    property var cfg_launchersDefault: []

    component Percent: RowLayout {
        property alias value: slider.value
        property alias from: slider.from
        property alias to: slider.to
        property string unit: " %"
        QQC2.Slider { id: slider; stepSize: 1; Layout.preferredWidth: Kirigami.Units.gridUnit * 12 }
        QQC2.Label { text: Math.round(slider.value) + parent.unit; Layout.minimumWidth: Kirigami.Units.gridUnit * 3 }
    }

    Kirigami.FormLayout {
        Item { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18nd("jarvis-desktop", "Applications") }
        QQC2.CheckBox { id: byScreen; text: i18nd("jarvis-desktop", "Only the windows of this screen") }
        QQC2.CheckBox { id: grouping; text: i18nd("jarvis-desktop", "One button per application (a click lists its windows)") }
        ColumnLayout {
            Kirigami.FormData.label: i18nd("jarvis-desktop", "Pinned:")
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            // The applications' names (Plasma's favourites model; without it, the file names).
            Loader {
                id: appNames
                source: "AppNames.qml"
                onLoaded: item.ids = Qt.binding(() => cfg_launchers.filter(u => u.startsWith("applications:")).map(u => u.substring(13)))
            }
            Repeater {
                model: cfg_launchers
                RowLayout {
                    required property int index
                    required property string modelData
                    QQC2.Label {
                        readonly property string id: modelData.replace(/^applications:/, "")
                        text: appNames.item && appNames.item.names[id] ? appNames.item.names[id] : id.replace(/\.desktop$/, "")
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        QQC2.ToolTip.text: modelData; QQC2.ToolTip.visible: nameHover.hovered; QQC2.ToolTip.delay: 600
                        HoverHandler { id: nameHover }
                    }
                    QQC2.ToolButton {
                        icon.name: "go-up"; enabled: index > 0
                        onClicked: { const l = cfg_launchers.slice(); l.splice(index - 1, 0, l.splice(index, 1)[0]); cfg_launchers = l }
                        QQC2.ToolTip.text: i18nd("jarvis-desktop", "Move up"); QQC2.ToolTip.visible: hovered
                    }
                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        onClicked: { const l = cfg_launchers.slice(); l.splice(index, 1); cfg_launchers = l }
                        QQC2.ToolTip.text: i18nd("jarvis-desktop", "Unpin"); QQC2.ToolTip.visible: hovered
                    }
                }
            }
            QQC2.Label {
                text: i18nd("jarvis-desktop", "Pin more from the launcher or a running application's menu (right click), or drop a .desktop file on the dock.")
                wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.7
            }
        }

        Item { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18nd("jarvis-desktop", "Look") }
        Percent { id: iconSize; from: 40; to: 85; Kirigami.FormData.label: i18nd("jarvis-desktop", "Icon size:") }
        Percent { id: gapSlider; from: 0; to: 16; unit: " px"; Kirigami.FormData.label: i18nd("jarvis-desktop", "Gap between icons:") }
        Percent { id: paddingSlider; from: 0; to: 10; unit: " px"; Kirigami.FormData.label: i18nd("jarvis-desktop", "Inner margin:") }
        Percent { id: glassSlider; from: 30; to: 100; Kirigami.FormData.label: i18nd("jarvis-desktop", "Glass opacity:") }
        Percent { id: glow; from: 0; to: 150; Kirigami.FormData.label: i18nd("jarvis-desktop", "Glow:") }
        QQC2.CheckBox { id: jarvis; text: i18nd("jarvis-desktop", "The Jarvis core button") }
        QQC2.CheckBox { id: profiles; text: i18nd("jarvis-desktop", "Workspaces (GENERAL, STUDY, WORK, DEV)") }
        QQC2.CheckBox { id: desktops; text: i18nd("jarvis-desktop", "Virtual desktops") }

        Item { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18nd("jarvis-desktop", "Motion") }
        QQC2.CheckBox { id: effects; text: i18nd("jarvis-desktop", "Animations (off: nothing moves)") }
        QQC2.CheckBox { id: magnification; enabled: effects.checked; text: i18nd("jarvis-desktop", "Magnify icons under the pointer") }
        Percent { id: magnifyAmount; enabled: effects.checked && magnification.checked; from: 5; to: 60; Kirigami.FormData.label: i18nd("jarvis-desktop", "Magnification:") }

        Item { Kirigami.FormData.isSection: true; Kirigami.FormData.label: i18nd("jarvis-desktop", "Panel") }
        QQC2.CheckBox {
            id: panelStyle
            text: i18nd("jarvis-desktop", "Jarvis glass under the whole panel (system tray and clock too)")
        }
        QQC2.Label {
            text: i18nd("jarvis-desktop", "Off: the tray and the clock stand on Plasma's own panel background.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            opacity: 0.7
        }
        QQC2.Button {
            text: i18nd("jarvis-desktop", "Panel settings…")
            icon.name: "configure"
            enabled: !!Plasmoid.containment && !!Plasmoid.containment.internalAction("configure")
            onClicked: Plasmoid.containment.internalAction("configure").trigger()
        }
        QQC2.Label {
            text: i18nd("jarvis-desktop", "Edge (bottom, top, left, right), thickness, alignment, auto-hide or always visible: Plasma's panel editor, or Jarvis → Settings → Desktop. Both apply at once.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            opacity: 0.7
        }
    }
}
