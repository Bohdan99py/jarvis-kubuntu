import QtQuick
import QtQuick.Layouts
import "theme.js" as Theme

// The widget's face on the desktop (or in the panel's popup): the mini core,
// Jarvis's state, system activity, the last notification and the buttons.
// Plain Qt Quick: Plasma-specific parts (D-Bus, windows, battery) live in
// main.qml, so this view is also checked outside Plasma (tests/plasmoid_check.cpp).
Rectangle {
    id: view
    property bool online: false
    property var status: ({})           // org.jarvis.Integration1.Status()
    property var metrics: ({})          // SystemMetrics()
    property string intensity: "normal" // what the core may do now
    property bool covered: false        // windows cover the desktop: the core rests
    signal overlayRequested()
    signal openRequested()
    signal startRequested()

    // Plasma provides i18n(); the test harness provides the same function.
    function t(text, arg) { return arg === undefined ? i18n(text) : i18n(text, arg) }

    readonly property bool integrated: online && view.status.integration === true
    readonly property bool outdated: online && view.status.version === undefined
    readonly property string hudState: online ? (view.status.state || "idle") : "offline"
    readonly property bool roomy: height >= 330
    readonly property var notice: view.status.notice || null
    property double now: Date.now()

    radius: 18
    color: Theme.glass
    border.color: Theme.line
    border.width: 1

    function stateName(s) {
        switch (s) {
        case "processing": return t("processing")
        case "notification": return t("notification")
        case "error": return t("error")
        case "offline": return t("offline")
        default: return t("idle")
        }
    }
    function levelName(level) {
        switch (level) {
        case "off": return t("off")
        case "assistive": return t("assistive")
        case "proactive": return t("proactive")
        default: return t("manual")
        }
    }
    function ago(at) {
        const minutes = Math.floor((view.now - at) / 60000)
        if (minutes < 1) return t("just now")
        if (minutes < 60) return t("%1 min ago", minutes)
        return t("%1 h ago", Math.floor(minutes / 60))
    }
    readonly property var chips: {
        const list = []
        if (!integrated) return list
        const aw = view.status.awareness || {}
        list.push(t("awareness: %1", levelName(aw.level)))
        if (aw.paused) list.push(t("paused"))
        if (aw.dnd) list.push(t("do not disturb"))
        if (view.status.animation && view.status.animation.onBattery) list.push(t("on battery"))
        if (view.covered) list.push(t("resting under windows"))
        return list
    }

    // Relative times stay right without waking more than twice a minute.
    Timer { interval: 30000; repeat: true; running: view.visible && view.notice !== null; onTriggered: view.now = Date.now() }

    component Meter: RowLayout {
        id: meter
        property string label
        property real value: -1   // 0..100, -1 unknown
        property string valueText: value < 0 ? "—" : Math.round(value) + "%"
        Layout.fillWidth: true
        spacing: 8
        Text { text: meter.label; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 11; Layout.preferredWidth: 70; elide: Text.ElideRight }
        Rectangle {
            Layout.fillWidth: true
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.08)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(100, meter.value)) / 100
                height: parent.height
                radius: 2
                color: meter.value > 85 ? Theme.amber : Theme.cyan
            }
        }
        Text { text: meter.valueText; color: Theme.text; font.family: Theme.mono; font.pixelSize: 11; Layout.preferredWidth: 44; horizontalAlignment: Text.AlignRight }
    }

    component HudButton: Rectangle {
        id: button
        property string text
        property bool primary: false
        signal clicked()
        Layout.fillWidth: true
        implicitHeight: 32
        radius: 9
        color: !enabled ? "transparent" : mouse.containsMouse ? Qt.rgba(0.37, 0.89, 1, 0.22) : primary ? Qt.rgba(0.37, 0.89, 1, 0.12) : "transparent"
        border.color: enabled ? (primary ? Theme.lineStrong : Theme.line) : Qt.rgba(1, 1, 1, 0.1)
        opacity: enabled ? 1 : 0.5
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.onPressAction: if (enabled) clicked()
        Text {
            anchors.centerIn: parent
            width: parent.width - 12
            text: button.text
            color: Theme.text
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: button.enabled; cursorShape: Qt.PointingHandCursor; onClicked: button.clicked() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        MiniCore {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 56
            Layout.preferredHeight: 150
            hudState: view.hudState
            intensity: view.intensity
            online: view.online
            cpu: view.integrated && view.metrics.cpu !== undefined ? view.metrics.cpu : -1
            memory: view.integrated && view.metrics.memory !== undefined ? view.metrics.memory : -1
        }
        Text {
            Layout.fillWidth: true
            text: "J.A.R.V.I.S."
            color: Theme.text
            font.family: Theme.mono
            font.pixelSize: 15
            font.bold: true
            font.letterSpacing: 4
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            Layout.fillWidth: true
            text: view.stateName(view.hudState).toUpperCase()
            color: Theme.stateColor(view.hudState)
            font.family: Theme.mono
            font.pixelSize: 11
            font.letterSpacing: 2
            horizontalAlignment: Text.AlignHCenter
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            visible: view.chips.length > 0
            Repeater {
                model: view.chips
                Rectangle {
                    required property string modelData
                    width: chipText.implicitWidth + 12
                    height: chipText.implicitHeight + 4
                    radius: height / 2
                    color: "transparent"
                    border.color: Theme.line
                    Text { id: chipText; anchors.centerIn: parent; text: parent.modelData; color: Theme.textDim; font.pixelSize: 10 }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            visible: view.integrated && view.roomy
            Meter { label: view.t("CPU"); value: view.metrics.cpu !== undefined ? view.metrics.cpu : -1 }
            Meter { label: view.t("Memory"); value: view.metrics.memory !== undefined ? view.metrics.memory : -1 }
            Meter {
                label: view.t("Heat")
                readonly property real celsius: view.metrics.temperature !== undefined ? view.metrics.temperature : -1
                value: celsius < 0 ? -1 : celsius   // the bar reads 100 °C as full
                valueText: celsius < 0 ? "—" : Math.round(celsius) + "°C"
            }
            Meter {
                visible: !!(view.metrics.battery && view.metrics.battery.present)
                label: view.t("Battery")
                value: view.metrics.battery ? view.metrics.battery.percent : -1
            }
        }
        Rectangle {
            Layout.fillWidth: true
            visible: view.integrated && view.notice !== null && view.roomy
            implicitHeight: noticeColumn.implicitHeight + 12
            radius: 10
            color: Theme.glassStrong
            border.color: Theme.line
            ColumnLayout {
                id: noticeColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: view.notice ? view.t("Last notification · %1", view.ago(view.notice.at)) : ""
                    color: Theme.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: view.notice ? view.notice.title : ""
                    color: Theme.text
                    font.pixelSize: 12
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textDim
            font.pixelSize: 11
            text: !view.online ? view.t("Jarvis is not running.")
                : view.outdated ? view.t("This Jarvis service has no desktop integration yet: restart it after the update.")
                : !view.integrated ? view.t("The desktop integration is off: Jarvis → Control center → Desktop.")
                : ""
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            HudButton {
                text: view.t("Overlay")
                primary: true
                enabled: view.integrated
                onClicked: view.overlayRequested()
            }
            HudButton {
                text: view.online ? view.t("Open Jarvis") : view.t("Start Jarvis")
                enabled: !view.online || view.integrated
                onClicked: view.online ? view.openRequested() : view.startRequested()
            }
        }
    }
}
