import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

// The settings page every Jarvis desktop widget shares (Plasma only). The
// widget's own configGeneral.qml gives the texts (its translations).
KCM.SimpleKCM {
    property alias cfg_effects: effects.checked
    property alias cfg_expanded: expanded.checked
    property alias cfg_extra: extra.checked
    property string effectsText
    property string expandedText
    property string extraText
    property string note

    Kirigami.FormLayout {
        QQC2.CheckBox { id: effects; text: effectsText }
        QQC2.CheckBox { id: expanded; text: expandedText; visible: expandedText.length > 0 }
        QQC2.CheckBox { id: extra; text: extraText; visible: extraText.length > 0 }
        QQC2.Label { text: note; visible: note.length > 0; wrapMode: Text.Wrap; Layout.maximumWidth: Kirigami.Units.gridUnit * 24; opacity: 0.7 }
    }
}
