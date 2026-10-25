import QtQuick
import QtQuick.Layouts
import org.jarvis.visual as JV

// What plays (any MPRIS player: Firefox, VLC, Spotify, Elisa…): the local
// cover, title, artist, controls; several players — a switch to the next one;
// the output device and its volume (PipeWire).
Item {
    id: view
    property var telemetry: ({})
    property var status: ({})
    property bool online: false
    property bool effects: true
    property bool extra: true
    signal action(string name, string arg)
    implicitHeight: panel.height

    readonly property var media: telemetry.media || {}
    readonly property var audio: media.audio || {}
    readonly property var players: media.players || []

    JV.HolographicPanel {
        id: panel
        width: parent.width
        panelWidth: parent.width
        title: i18nd("jarvis-desktop", "MEDIA")
        tag: view.media.player ? view.media.player.toUpperCase() : ""
        shown: true
        movable: false
        busy: view.effects && view.media.status === "Playing"
        Column {
            width: parent.width
            spacing: Math.round(6 * JV.JarvisTheme.spacing)
            RowLayout {
                width: parent.width
                spacing: 12
                // The cover: the track's own (a local file only), a note otherwise.
                Rectangle {
                    id: cover
                    visible: view.extra
                    Layout.preferredWidth: Math.round(76 * JV.JarvisTheme.spacing)
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: JV.JarvisTheme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.05)
                    border.color: JV.JarvisTheme.edge
                    Image {
                        id: art
                        anchors.fill: parent; anchors.margins: 2
                        source: view.media.artUrl || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: 160
                        visible: status === Image.Ready
                        opacity: visible ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 250 } }
                    }
                    Text { anchors.centerIn: parent; text: "♪"; color: JV.JarvisTheme.textSoft; font.pixelSize: 28; visible: art.status !== Image.Ready }
                }
                Column {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        width: parent.width
                        text: !view.online ? i18nd("jarvis-desktop", "Jarvis is not running.")
                            : view.media.available ? (view.media.title || i18nd("jarvis-desktop", "Unknown track"))
                            : i18nd("jarvis-desktop", "Nothing is playing")
                        color: JV.JarvisTheme.text; font.pixelSize: JV.JarvisTheme.fontBody + 1; font.bold: true
                        elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.Wrap; textFormat: Text.PlainText
                    }
                    Text {
                        width: parent.width
                        visible: !!view.media.artist || !!view.media.album
                        text: [view.media.artist, view.media.album].filter(x => !!x).join(" — ")
                        color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall + 1; elide: Text.ElideRight; textFormat: Text.PlainText
                    }
                    Row {
                        spacing: 6
                        visible: !!view.media.available
                        JV.HoloButton { glyph: "⏮"; enabled: !!view.media.canPrevious; onClicked: view.action("previous", ""); Accessible.name: i18nd("jarvis-desktop", "Previous") }
                        JV.HoloButton { glyph: view.media.status === "Playing" ? "⏸" : "▶"; primary: true; enabled: !!view.media.canPlay; onClicked: view.action("play-pause", ""); Accessible.name: i18nd("jarvis-desktop", "Play or pause") }
                        JV.HoloButton { glyph: "⏭"; enabled: !!view.media.canNext; onClicked: view.action("next", ""); Accessible.name: i18nd("jarvis-desktop", "Next") }
                        JV.HoloButton {
                            visible: view.players.length > 1
                            glyph: "⇄"
                            text: i18nd("jarvis-desktop", "%1 players", view.players.length)
                            onClicked: view.action("next-player", "")
                            Accessible.name: i18nd("jarvis-desktop", "Switch to the next player")
                        }
                    }
                }
            }
            // The output and its volume.
            RowLayout {
                width: parent.width
                visible: view.audio.output !== undefined
                spacing: 6
                JV.HoloButton { glyph: view.audio.muted ? "🔇" : "🔊"; checked: !!view.audio.muted; onClicked: view.action("mute", ""); Accessible.name: i18nd("jarvis-desktop", "Mute") }
                JV.HoloButton { glyph: "−"; onClicked: view.action("volume-down", ""); Accessible.name: i18nd("jarvis-desktop", "Quieter") }
                JV.HoloMeter {
                    Layout.fillWidth: true
                    label: view.audio.muted ? i18nd("jarvis-desktop", "muted") : i18nd("jarvis-desktop", "Volume")
                    value: view.audio.volume !== undefined ? view.audio.volume : -1
                    warnAt: 101
                    animated: view.effects
                }
                JV.HoloButton { glyph: "+"; onClicked: view.action("volume-up", ""); Accessible.name: i18nd("jarvis-desktop", "Louder") }
            }
            Text {
                width: parent.width
                visible: view.audio.output !== undefined
                text: view.audio.output || ""
                color: JV.JarvisTheme.textSoft; font.pixelSize: JV.JarvisTheme.fontSmall; elide: Text.ElideRight
            }
        }
    }
}
