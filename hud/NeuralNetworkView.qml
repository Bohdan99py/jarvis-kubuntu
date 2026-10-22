import QtQuick

// The memory at work. When Jarvis recalls (taught answers, facts, earlier
// conversations) or writes to memory, the strongest words of the real memory
// graph light up around the core, connected as they are in memory; light runs
// along the links and towards the core, then the network fades.
// Nodes are real memory words (chatModel.graph); with an empty memory only the
// links to the core flash.
Item {
    id: net
    required property var ctl
    property string graph: "{}"
    property real reveal: 0
    property string mode: ""          // recall, write
    property var nodes: []            // [{x, y, label, kind, w}]
    property var edges: []            // [{a, b}] indices into nodes; -1 is the core
    readonly property real size: Math.min(width, height)
    readonly property point centre: Qt.point(width / 2, height / 2)

    function colorOf(kind) {
        return kind === "fact" ? "#ffd27a" : kind === "topic" ? "#b9a8ff" : "#7ff0ff"
    }

    // Lights the network up: `mode` recall or write.
    function flare(newMode) {
        let g
        try { g = JSON.parse(graph) } catch (e) { g = {} }
        const all = (g.nodes || []).map((n, i) => ({ i: i, label: n.label, kind: n.kind, w: n.weight || 1 }))
        all.sort((a, b) => b.w - a.w)
        const chosen = all.slice(0, ctl.low ? 7 : 12)
        const index = {}
        const placed = []
        const turn = Math.random() * Math.PI * 2
        chosen.forEach((n, k) => {
            const a = turn + k / Math.max(1, chosen.length) * Math.PI * 2 + (Math.random() - 0.5) * 0.35
            const r = size * (0.27 + 0.13 * ((k * 7) % 3) / 2 + Math.random() * 0.05)
            index[n.i] = placed.length
            placed.push({ x: centre.x + Math.cos(a) * r, y: centre.y + Math.sin(a) * r, label: n.label, kind: n.kind, w: n.w })
        })
        const links = []
        for (const e of (g.edges || [])) {
            if (index[e.source] !== undefined && index[e.target] !== undefined && links.length < 24)
                links.push({ a: index[e.source], b: index[e.target] })
        }
        for (let k = 0; k < placed.length; k += 2)
            links.push({ a: k, b: -1 })
        if (placed.length === 0) // no memory yet: a few links to the core only
            for (let k = 0; k < 6; ++k) {
                const a = turn + k * Math.PI / 3
                placed.push({ x: centre.x + Math.cos(a) * size * 0.34, y: centre.y + Math.sin(a) * size * 0.34, label: "", kind: "example", w: 1 })
                links.push({ a: k, b: -1 })
            }
        nodes = placed
        edges = links
        mode = newMode
        life.restart()
    }

    SequentialAnimation {
        id: life
        NumberAnimation { target: net; property: "reveal"; to: 1; duration: 520; easing.type: Easing.OutCubic }
        PauseAnimation { duration: net.ctl.low ? 1800 : 3400 }
        NumberAnimation { target: net; property: "reveal"; to: 0; duration: 1100; easing.type: Easing.InCubic }
    }

    visible: reveal > 0.001

    // Links: thin rotated bars, with a light running along each.
    Repeater {
        model: net.visible ? net.edges : []
        delegate: Item {
            id: link
            required property var modelData
            required property int index
            readonly property var from: net.nodes[modelData.a] || net.centre
            readonly property var to: modelData.b < 0 ? net.centre : (net.nodes[modelData.b] || net.centre)
            readonly property real dx: to.x - from.x
            readonly property real dy: to.y - from.y
            readonly property real length: Math.sqrt(dx * dx + dy * dy)
            x: from.x
            y: from.y
            width: length * Math.min(1, net.reveal * 1.6 - index * 0.02)
            height: 1
            rotation: Math.atan2(dy, dx) * 180 / Math.PI
            transformOrigin: Item.Left
            opacity: net.reveal
            Rectangle {
                anchors.fill: parent
                color: link.modelData.b < 0 ? net.ctl.colorA : "#9fdcff"
                opacity: link.modelData.b < 0 ? 0.35 : 0.5
            }
            Rectangle {
                width: 14; height: 3; radius: 1.5
                y: -1
                color: "#ffffff"
                opacity: 0.85
                // Writes flow into the core, recall flows out of it.
                x: ((net.mode === "write" ? 1 : -1) * net.ctl.time * 0.9 * (0.6 + (link.index % 4) * 0.15) % 1 + 1) % 1 * Math.max(0, link.width - width)
            }
        }
    }

    // Nodes with their words.
    Repeater {
        model: net.visible ? net.nodes : []
        delegate: Item {
            id: node
            required property var modelData
            required property int index
            x: modelData.x
            y: modelData.y
            opacity: Math.min(1, Math.max(0, net.reveal * 1.8 - index * 0.05))
            scale: 0.4 + 0.6 * opacity
            Rectangle {
                anchors.centerIn: parent
                width: 7 + Math.min(8, node.modelData.w); height: width; radius: width / 2
                color: net.colorOf(node.modelData.kind)
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 2.6; height: width; radius: width / 2
                    color: "transparent"
                    border.color: parent.color
                    border.width: 1
                    opacity: 0.35
                }
            }
            Text {
                visible: !net.ctl.low && node.modelData.label.length > 0
                x: 10; y: -18
                text: node.modelData.label
                color: net.colorOf(node.modelData.kind)
                font.family: JarvisTheme.mono
                font.pixelSize: 11
                style: Text.Outline
                styleColor: "#02060c"
            }
        }
    }
}
