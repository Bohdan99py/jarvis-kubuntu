import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    property alias cfg_effects: effects.checked
    property alias cfg_expanded: expanded.checked
    property alias cfg_extra: extra.checked
    property alias cfg_backdrop: backdrop.value
    property alias cfg_backdropSoft: soft.checked

    Kirigami.FormLayout {
        QQC2.CheckBox { id: effects; text: i18nd("jarvis-desktop", "Animate the core") }
        QQC2.CheckBox { id: expanded; text: i18nd("jarvis-desktop", "Show the state and the workspace under the core") }
        QQC2.CheckBox { id: extra; text: i18nd("jarvis-desktop", "CPU and memory arcs around the core") }
        RowLayout {
            Kirigami.FormData.label: i18nd("jarvis-desktop", "Shade under the core:")
            QQC2.Slider { id: backdrop; from: 0; to: 100; stepSize: 5; Layout.preferredWidth: Kirigami.Units.gridUnit * 12 }
            QQC2.Label { text: backdrop.value === 0 ? i18nd("jarvis-desktop", "off") : Math.round(backdrop.value) + " %" }
        }
        QQC2.CheckBox { id: soft; text: i18nd("jarvis-desktop", "Soft, wide falloff"); enabled: backdrop.value > 0 }
        QQC2.Label {
            text: i18nd("jarvis-desktop", "The shade is denser on a light wallpaper (Jarvis measures it) and fades out before the widget's edge. Quality, theme and size: Jarvis → Settings → Desktop. The core rests while a maximized or fullscreen window covers the desktop.")
            wrapMode: Text.Wrap; Layout.maximumWidth: Kirigami.Units.gridUnit * 24; opacity: 0.7
        }
    }
}
