import QtQuick
import org.jarvis.visual as JV

// What plays (any MPRIS player: Firefox, VLC, Spotify, Elisa…): local cover,
// title, artist, controls, the output device and its volume (PipeWire).
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)

    readonly property var media: telemetry.media || {}
    readonly property var audio: media.audio || {}

    JV.HolographicPanel {
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "MEDIA")
        tag: view.media.player ? view.media.player.toUpperCase() : ""
        shown: true
        movable: false
        busy: view.effects && view.media.status === "Playing"
        Row {
            width: parent.width
            spacing: 10
            Rectangle {
                id: cover
                visible: view.extra
                width: 64; height: 64
                color: Qt.rgba(1, 1, 1, 0.05)
                border.color: JV.JarvisTheme.line
                Image {
                    anchors.fill: parent; anchors.margins: 1
                    source: view.media.artUrl || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 128
                    visible: status === Image.Ready
                }
                Text { anchors.centerIn: parent; text: "♪"; color: JV.JarvisTheme.textDim; font.pixelSize: 24; visible: !view.media.artUrl }
            }
            Column {
                width: parent.width - (cover.visible ? cover.width + 10 : 0)
                spacing: 3
                Text {
                    width: parent.width
                    text: !view.online ? i18nd("jarvis-desktop", "Jarvis is not running.")
                        : view.media.available ? (view.media.title || i18nd("jarvis-desktop", "Unknown track"))
                        : i18nd("jarvis-desktop", "Nothing is playing")
                    color: JV.JarvisTheme.text; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: !!view.media.artist || !!view.media.album
                    text: [view.media.artist, view.media.album].filter(x => !!x).join(" — ")
                    color: JV.JarvisTheme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                }
                Row {
                    spacing: 4
                    visible: !!view.media.available
                    JV.HoloButton { glyph: "⏮"; enabled: !!view.media.canPrevious; onClicked: view.action("previous", "") }
                    JV.HoloButton { glyph: view.media.status === "Playing" ? "⏸" : "▶"; primary: true; enabled: !!view.media.canPlay; onClicked: view.action("play-pause", "") }
                    JV.HoloButton { glyph: "⏭"; enabled: !!view.media.canNext; onClicked: view.action("next", "") }
                }
            }
        }
        Column {
            y: 74
            width: parent.width
            spacing: 3
            visible: view.audio.output !== undefined
            Text {
                width: parent.width
                text: "🔈 " + (view.audio.output || "")
                color: JV.JarvisTheme.textDim; font.pixelSize: 10; elide: Text.ElideRight
            }
            Row {
                spacing: 4
                JV.HoloButton { glyph: "−"; onClicked: view.action("volume-down", "") }
                JV.HoloMeter {
                    width: 140
                    label: view.audio.muted ? i18nd("jarvis-desktop", "muted") : i18nd("jarvis-desktop", "Volume")
                    value: view.audio.volume !== undefined ? view.audio.volume : -1
                    warnAt: 101
                    animated: view.effects
                }
                JV.HoloButton { glyph: "+"; onClicked: view.action("volume-up", "") }
                JV.HoloButton { glyph: view.audio.muted ? "🔇" : "🔊"; checked: !!view.audio.muted; onClicked: view.action("mute", "") }
            }
        }
    }
}
