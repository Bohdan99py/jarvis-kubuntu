import QtQuick
import QtQuick.Controls
import Jarvis

// Memory graph: words Jarvis learned and how often they come up together.
// A force layout settles once when the memory changes and then stops, so an
// idle graph costs nothing. Wheel zooms around the cursor, dragging the
// background pans, nodes can be dragged, clicked (shows their links) and
// double-clicked (zooms in on them). Only the strongest links of each word are
// drawn by default; the rest stay faint until a word is focused.
Rectangle {
    id: graph
    color: "#0c1523"
    radius: 20
    border.color: Theme.border
    clip: true

    property string graphData: "{}"
    property var network: ({nodes: [], edges: [], examples: 0})

    // Layout, indexed like network.nodes. Plain arrays: `frame` tells bindings they moved.
    property var px: []
    property var py: []
    property var degree: []
    property var neighbors: []    // per node: [{index, weight, edge}], strongest first
    property var backbone: ({})   // edge index -> true for the links drawn by default
    property var rank: []         // node index -> place by weight (0 = heaviest)
    property real maxEdgeWeight: 1
    property int frame: 0
    property real temperature: 0
    property int dragged: -1

    property int selected: -1
    property int hovered: -1
    // Focus: the clicked word, or the one under the mouse while nothing is clicked.
    readonly property int focusNode: selected >= 0 ? selected : hovered
    readonly property var focusSet: {
        const set = {}
        if (focusNode >= 0 && focusNode < neighbors.length)
            neighbors[focusNode].forEach(function(n) { set[n.index] = true })
        return set
    }
    property var hiddenKinds: ({})
    property bool allLinks: false
    readonly property string query: search.text.trim().toLowerCase()
    property real pulse: 0

    // View: screen = world * zoom + (width / 2 + panX, height / 2 + panY).
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 5

    function kindColor(kind) {
        // Strings: Canvas styles take CSS colors.
        return "" + (kind === "fact" ? Theme.nodeFact : kind === "topic" ? Theme.nodeTopic : Theme.nodeExample)
    }
    function kindName(kind) {
        return kind === "fact" ? qsTr("fact about you") : kind === "topic" ? qsTr("chat topic") : qsTr("taught example")
    }
    // `frame` is read so that bindings using these follow the layout.
    function sx(i) { return frame >= 0 ? (px[i] || 0) * zoom + width / 2 + panX : 0 }
    function sy(i) { return frame >= 0 ? (py[i] || 0) * zoom + height / 2 + panY : 0 }
    function shown(i) { return i >= 0 && i < network.nodes.length && !hiddenKinds[network.nodes[i].kind] }

    onGraphDataChanged: rebuild()
    // Until the user moves the view, it follows the size of the panel.
    property bool viewTouched: false
    onWidthChanged: { if (!viewTouched) fit(false); canvas.requestPaint() }
    onHeightChanged: { if (!viewTouched) fit(false); canvas.requestPaint() }
    onFrameChanged: canvas.requestPaint()
    onZoomChanged: canvas.requestPaint()
    onPanXChanged: canvas.requestPaint()
    onPanYChanged: canvas.requestPaint()
    onFocusNodeChanged: canvas.requestPaint()
    onHiddenKindsChanged: canvas.requestPaint()
    onAllLinksChanged: canvas.requestPaint()
    onPulseChanged: canvas.requestPaint()

    function rebuild() {
        let data
        try { data = JSON.parse(graphData) } catch (e) { data = null }
        if (!data || !Array.isArray(data.nodes))
            data = {nodes: [], edges: [], examples: 0}
        data.edges = (data.edges || []).filter(function(e) {
            return e.source >= 0 && e.target >= 0 && e.source < data.nodes.length && e.target < data.nodes.length
        })
        const nodes = data.nodes, edges = data.edges, n = nodes.length

        // Keep the positions and the focus of words that are still there.
        const old = {}
        network.nodes.forEach(function(node, i) { old[node.label] = {x: px[i], y: py[i]} })
        const selectedLabel = selected >= 0 && selected < network.nodes.length ? network.nodes[selected].label : ""

        const adj = [], deg = []
        let maxW = 1
        for (let i = 0; i < n; i++) { adj.push([]); deg.push(0) }
        edges.forEach(function(e, k) {
            adj[e.source].push({index: e.target, weight: e.weight, edge: k})
            adj[e.target].push({index: e.source, weight: e.weight, edge: k})
            deg[e.source]++; deg[e.target]++
            maxW = Math.max(maxW, e.weight)
        })
        // Strongest links first; among equals, links to heavier words.
        adj.forEach(function(list) {
            list.sort(function(a, b) {
                return b.weight - a.weight || nodes[b.index].weight - nodes[a.index].weight || a.index - b.index
            })
        })
        // Every word keeps its two strongest links: one taught paragraph links all of
        // its words to each other, and drawing that whole web is what makes a hairball.
        const bones = {}
        adj.forEach(function(list) { list.slice(0, 2).forEach(function(l) { bones[l.edge] = true }) })
        const order = nodes.map(function(node, i) { return i })
        order.sort(function(a, b) { return nodes[b].weight - nodes[a].weight || a - b })
        const ranks = []
        order.forEach(function(nodeIndex, place) { ranks[nodeIndex] = place })

        const x = [], y = []
        let fresh = 0
        for (let i = 0; i < n; i++) {
            const known = old[nodes[i].label]
            if (known && isFinite(known.x)) { x.push(known.x); y.push(known.y); continue }
            fresh++
            x.push(NaN); y.push(NaN)
        }
        for (let i = 0; i < n; i++) {
            if (!isNaN(x[i]))
                continue
            // A new word starts next to the words it is linked to, or on a spiral.
            let cx = 0, cy = 0, count = 0
            adj[i].forEach(function(l) { if (!isNaN(x[l.index])) { cx += x[l.index]; cy += y[l.index]; count++ } })
            if (count > 0) {
                x[i] = cx / count + (Math.random() - 0.5) * 30
                y[i] = cy / count + (Math.random() - 0.5) * 30
            } else {
                const angle = i * 2.399963, r = 14 * Math.sqrt(i + 1)
                x[i] = Math.cos(angle) * r
                y[i] = Math.sin(angle) * r
            }
        }

        px = x; py = y; degree = deg; neighbors = adj; backbone = bones; rank = ranks; maxEdgeWeight = maxW
        dragged = -1
        hovered = -1
        network = data
        selected = -1
        for (let i = 0; i < n && selectedLabel.length > 0; i++)
            if (nodes[i].label === selectedLabel) selected = i

        if (fresh === n && n > 0) {
            // First layout: settle off screen, then show the result already fitted.
            temperature = 40
            for (let k = 0; k < 220; k++) step()
            viewTouched = false
            fit(false)
        } else if (fresh > 0) {
            temperature = Math.max(temperature, 18)
        }
        frame++
    }

    // One step of a Fruchterman–Reingold layout: words push each other apart,
    // links pull them together, a weak gravity keeps loose words in view.
    function step() {
        const n = px.length, k = 50, k2 = k * k, reach = 16 * k2
        const fx = new Array(n).fill(0), fy = new Array(n).fill(0)
        for (let i = 0; i < n; i++) {
            fx[i] -= px[i] * 0.06; fy[i] -= py[i] * 0.06
            for (let j = i + 1; j < n; j++) {
                let dx = px[i] - px[j], dy = py[i] - py[j], d2 = dx * dx + dy * dy
                if (d2 < 1) { dx = Math.random() - 0.5; dy = Math.random() - 0.5; d2 = 1 }
                if (d2 > reach)
                    continue
                const f = k2 / d2
                fx[i] += dx * f; fy[i] += dy * f; fx[j] -= dx * f; fy[j] -= dy * f
            }
        }
        const edges = network.nodes.length === n ? network.edges : []
        for (let e = 0; e < edges.length; e++) {
            const a = edges[e].source, b = edges[e].target
            const dx = px[b] - px[a], dy = py[b] - py[a], d = Math.sqrt(dx * dx + dy * dy) || 1
            // Big cliques pull weaker per link, or a long paragraph collapses into a dot.
            const f = d / k * edges[e].weight / Math.pow(Math.min(degree[a], degree[b]), 0.8)
            fx[a] += dx * f; fy[a] += dy * f; fx[b] -= dx * f; fy[b] -= dy * f
        }
        for (let i = 0; i < n; i++) {
            if (i === dragged)
                continue
            const m = Math.sqrt(fx[i] * fx[i] + fy[i] * fy[i])
            if (m > 0) {
                const len = Math.min(m, temperature)
                px[i] += fx[i] / m * len
                py[i] += fy[i] / m * len
            }
        }
        temperature = dragged >= 0 ? Math.max(4, temperature * 0.97) : temperature * 0.95
    }

    Timer {
        interval: 16; repeat: true
        running: graph.visible && graph.temperature > 0.6
        onTriggered: { graph.step(); graph.frame++ }
    }
    // Signals travel along the links of the focused word; only while it is on screen.
    Timer {
        interval: 50; repeat: true
        running: graph.visible && graph.selected >= 0 && graph.Window.active
        onTriggered: graph.pulse = (graph.pulse + 0.035) % 1
    }

    // ---- View ----
    function clampZoom(z) { return Math.max(minZoom, Math.min(maxZoom, z)) }
    function zoomAt(mx, my, factor) {
        viewAnimation.stop()
        viewTouched = true
        const z = clampZoom(zoom * factor)
        const wx = (mx - width / 2 - panX) / zoom, wy = (my - height / 2 - panY) / zoom
        zoom = z
        panX = mx - width / 2 - wx * z
        panY = my - height / 2 - wy * z
    }
    function animateView(z, x, y) {
        viewAnimation.stop()
        viewTouched = true
        zoomAnimation.to = z; panXAnimation.to = x; panYAnimation.to = y
        viewAnimation.start()
    }
    function fit(animated) {
        let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity
        for (let i = 0; i < px.length; i++) {
            if (!shown(i)) continue
            x0 = Math.min(x0, px[i]); x1 = Math.max(x1, px[i]); y0 = Math.min(y0, py[i]); y1 = Math.max(y1, py[i])
        }
        if (!isFinite(x0)) return
        const z = clampZoom(Math.min(1.6, (width - 90) / Math.max(1, x1 - x0), (height - 130) / Math.max(1, y1 - y0)))
        const x = -(x0 + x1) / 2 * z, y = -(y0 + y1) / 2 * z
        if (animated) { animateView(z, x, y); viewTouched = false }
        else { viewAnimation.stop(); zoom = z; panX = x; panY = y }
    }
    function centerOn(i, closer) {
        if (i < 0 || i >= px.length) return
        const z = closer ? Math.max(zoom, 1.8) : zoom
        animateView(z, -px[i] * z, -py[i] * z - (card.visible ? card.height / 2 : 0))
    }
    function select(i) {
        selected = i
        if (i >= 0 && (sx(i) < 40 || sx(i) > width - 40 || sy(i) < 50 || sy(i) > height - 150))
            centerOn(i, false)
    }
    function toggleKind(kind) {
        const next = Object.assign({}, hiddenKinds)
        next[kind] = !next[kind]
        hiddenKinds = next
        if (selected >= 0 && !shown(selected)) selected = -1
        fit(true)
    }
    function findNext() {
        if (query.length === 0) return
        const nodes = network.nodes
        for (let k = 1; k <= nodes.length; k++) {
            const i = (Math.max(selected, -1) + k) % nodes.length
            if (shown(i) && nodes[i].label.toLowerCase().indexOf(query) >= 0) {
                selected = i
                centerOn(i, true)
                return
            }
        }
    }

    ParallelAnimation {
        id: viewAnimation
        NumberAnimation { id: zoomAnimation; target: graph; property: "zoom"; duration: 380; easing.type: Easing.OutCubic }
        NumberAnimation { id: panXAnimation; target: graph; property: "panX"; duration: 380; easing.type: Easing.OutCubic }
        NumberAnimation { id: panYAnimation; target: graph; property: "panY"; duration: 380; easing.type: Easing.OutCubic }
    }

    // ---- Links ----
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const edges = graph.network.edges || []
            const focus = graph.focusNode
            const ox = width / 2 + graph.panX, oy = height / 2 + graph.panY, z = graph.zoom
            const px = graph.px, py = graph.py
            // Batch by style: one stroke per group instead of one per link.
            const groups = [[], [], [], []]   // faint, normal, strong, focused
            const strong = graph.maxEdgeWeight / 2
            for (let k = 0; k < edges.length; k++) {
                const e = edges[k]
                if (!graph.shown(e.source) || !graph.shown(e.target)) continue
                const main = graph.allLinks || graph.backbone[k] === true
                if (focus >= 0 && (e.source === focus || e.target === focus)) groups[3].push(e)
                else if (focus >= 0) { if (main) groups[0].push(e) }
                else if (main) groups[graph.maxEdgeWeight > 1 && e.weight > strong ? 2 : 1].push(e)
                else groups[0].push(e)
            }
            const styles = [
                {color: focus >= 0 ? "rgba(60,110,140,0.08)" : "rgba(60,110,140,0.05)", width: 1},
                {color: "rgba(70,140,170,0.45)", width: 1.2},
                {color: "rgba(90,190,210,0.75)", width: 2.2},
                {color: "rgba(85,220,229,0.85)", width: 1.8}
            ]
            ctx.lineCap = "round"
            for (let g = 0; g < groups.length; g++) {
                if (groups[g].length === 0) continue
                ctx.strokeStyle = styles[g].color
                ctx.lineWidth = styles[g].width
                ctx.beginPath()
                groups[g].forEach(function(e) {
                    ctx.moveTo(px[e.source] * z + ox, py[e.source] * z + oy)
                    ctx.lineTo(px[e.target] * z + ox, py[e.target] * z + oy)
                })
                ctx.stroke()
            }
            if (graph.selected >= 0 && graph.selected === focus) {
                // Signals flow out of the selected word.
                ctx.fillStyle = "#b8f6fa"
                ctx.beginPath()
                groups[3].forEach(function(e, k) {
                    const from = e.source === focus ? e.source : e.target, to = from === e.source ? e.target : e.source
                    const t = (graph.pulse + k * 0.137) % 1
                    const x = (px[from] + (px[to] - px[from]) * t) * z + ox, y = (py[from] + (py[to] - py[from]) * t) * z + oy
                    ctx.moveTo(x + 2, y)
                    ctx.arc(x, y, 2, 0, Math.PI * 2)
                })
                ctx.fill()
            }
        }
    }

    // ---- Background: pan, zoom, deselect ----
    MouseArea {
        id: background
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: pressed && moved ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real lastX: 0
        property real lastY: 0
        property bool moved: false
        onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y; moved = false; viewAnimation.stop(); search.focus = false }
        onPositionChanged: function(mouse) {
            if (!moved && Math.abs(mouse.x - lastX) + Math.abs(mouse.y - lastY) < 4) return
            moved = true
            graph.viewTouched = true
            graph.panX += mouse.x - lastX; graph.panY += mouse.y - lastY
            lastX = mouse.x; lastY = mouse.y
        }
        onClicked: if (!moved) graph.selected = -1
        onDoubleClicked: graph.fit(true)
        onWheel: function(wheel) {
            const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y * 4
            graph.zoomAt(wheel.x, wheel.y, Math.pow(1.0018, delta))
        }
    }

    // ---- Words ----
    Repeater {
        model: graph.network.nodes
        Item {
            id: node
            required property int index
            required property var modelData
            readonly property bool focused: graph.focusNode === index
            readonly property bool hot: graph.hovered === index
            readonly property bool lit: graph.focusNode < 0 || focused || graph.focusSet[index] === true
            readonly property bool matched: graph.query.length > 0 && modelData.label.toLowerCase().indexOf(graph.query) >= 0
            readonly property real r: Math.min(13, 4 + Math.sqrt(modelData.weight) * 2) * Math.max(0.8, Math.min(1.5, Math.sqrt(graph.zoom)))
            readonly property color tint: graph.kindColor(modelData.kind)
            readonly property bool showLabel: focused || matched || hot
                                              || (graph.focusNode >= 0 && lit)
                                              || (graph.focusNode < 0 && graph.rank[index] < Math.max(8, Math.round(8 * graph.zoom * graph.zoom)))
            visible: !graph.hiddenKinds[modelData.kind]
            x: graph.sx(index) - width / 2
            y: graph.sy(index) - height / 2
            width: Math.max(26, r * 2 + 10)
            height: width
            z: focused || hot ? 3 : lit && graph.focusNode >= 0 ? 2 : 1
            opacity: lit || matched ? 1 : 0.22

            Rectangle {   // glow: a soft halo instead of a blurred shadow
                anchors.centerIn: parent
                width: node.r * 2 + (node.focused ? 18 : 10); height: width; radius: width / 2
                color: node.tint
                opacity: node.focused || node.matched ? 0.32 : 0.14
            }
            Rectangle {
                anchors.centerIn: parent
                width: node.r * 2; height: width; radius: width / 2
                color: node.focused ? "#ffffff" : node.tint
                border.width: graph.selected === node.index || node.matched ? 2 : 0
                border.color: node.matched && graph.selected !== node.index ? Theme.nodeFact : Theme.accent
                scale: node.hot ? 1.2 : 1
                Behavior on scale { NumberAnimation { duration: 110 } }
            }
            Rectangle {
                visible: node.showLabel
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height / 2 + node.r + 3
                width: label.implicitWidth + 10; height: label.implicitHeight + 4; radius: 6
                color: Qt.rgba(0.047, 0.082, 0.137, 0.78)
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: node.modelData.label
                    color: node.focused ? "#ffffff" : "#c0d2df"
                    font.pixelSize: node.focused ? 13 : 11
                    font.bold: node.focused
                }
            }
            MouseArea {
                id: hit
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                cursorShape: pressed && moved ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                property real startX: 0
                property real startY: 0
                property bool moved: false
                onEntered: graph.hovered = node.index
                onExited: if (graph.hovered === node.index) graph.hovered = -1
                onPressed: function(mouse) { startX = mouse.x; startY = mouse.y; moved = false; search.focus = false }
                onPositionChanged: function(mouse) {
                    if (!pressed || (!moved && Math.abs(mouse.x - startX) + Math.abs(mouse.y - startY) < 4)) return
                    moved = true
                    const p = mapToItem(graph, mouse.x, mouse.y)
                    graph.dragged = node.index
                    graph.px[node.index] = (p.x - graph.width / 2 - graph.panX) / graph.zoom
                    graph.py[node.index] = (p.y - graph.height / 2 - graph.panY) / graph.zoom
                    graph.temperature = Math.max(graph.temperature, 6)
                    graph.frame++
                }
                onReleased: if (graph.dragged === node.index) graph.dragged = -1
                onClicked: if (!moved) graph.select(graph.selected === node.index ? -1 : node.index)
                onDoubleClicked: { graph.selected = node.index; graph.centerOn(node.index, true) }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: graph.network.nodes.length === 0
        text: qsTr("Memory is empty for now\nChat with me or add a first example")
        color: Theme.textDim; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.5
    }

    // ---- Toolbar: search, links, legend that filters by kind ----
    Row {
        id: toolbar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
        spacing: 6
        visible: graph.network.nodes.length > 0
        TextField {
            id: search
            width: 130; height: 28
            leftPadding: 10; rightPadding: 8; topPadding: 0; bottomPadding: 0
            font.pixelSize: 12
            placeholderText: qsTr("Find a word…")
            color: Theme.text; placeholderTextColor: Theme.textDim
            background: Rectangle { color: Theme.bg; radius: 14; border.color: search.activeFocus ? Theme.accent : Theme.border }
            onAccepted: graph.findNext()
            Keys.onEscapePressed: { text = ""; focus = false }
        }
        Rectangle {
            width: linksText.implicitWidth + 18; height: 28; radius: 14
            color: linksMouse.containsMouse ? Theme.hover : graph.allLinks ? Theme.selected : Theme.bg
            border.color: graph.allLinks ? Theme.accent : Theme.border
            Text { id: linksText; anchors.centerIn: parent; text: graph.allLinks ? qsTr("All links") : qsTr("Strong links"); color: Theme.text; font.pixelSize: 11 }
            MouseArea { id: linksMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: graph.allLinks = !graph.allLinks }
            ToolTip.visible: linksMouse.containsMouse
            ToolTip.delay: 500
            ToolTip.text: qsTr("Show every link, or only the two strongest of each word")
        }
    }
    Rectangle {
        anchors.right: parent.right; anchors.top: toolbar.bottom; anchors.margins: 10; anchors.topMargin: 6
        width: legend.implicitWidth + 4; height: 26; radius: 13
        color: Qt.rgba(0.047, 0.082, 0.137, 0.8)
        visible: graph.network.nodes.length > 0
        Row {
            id: legend
            anchors.centerIn: parent
            spacing: 0
            Repeater {
                model: ["example", "fact", "topic"]
                Rectangle {
                    id: legendItem
                    required property string modelData
                    readonly property bool off: graph.hiddenKinds[modelData] === true
                    width: legendRow.implicitWidth + 14; height: 22; radius: 11
                    color: legendMouse.containsMouse ? Theme.hover : "transparent"
                    opacity: off ? 0.45 : 1
                    Row {
                        id: legendRow
                        anchors.centerIn: parent
                        spacing: 4
                        Rectangle { width: 8; height: 8; radius: 4; color: legendItem.off ? "transparent" : graph.kindColor(legendItem.modelData); border.color: graph.kindColor(legendItem.modelData); anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: legendItem.modelData === "example" ? qsTr("examples") : legendItem.modelData === "fact" ? qsTr("facts") : qsTr("topics")
                            color: Theme.textDim; font.pixelSize: 10; font.strikeout: legendItem.off
                        }
                    }
                    MouseArea { id: legendMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: graph.toggleKind(legendItem.modelData) }
                    ToolTip.visible: legendMouse.containsMouse
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Click to hide or show")
                }
            }
        }
    }

    // ---- Zoom buttons ----
    Column {
        anchors.right: parent.right; anchors.bottom: card.visible ? card.top : hint.top; anchors.margins: 10
        spacing: 4
        visible: graph.network.nodes.length > 0
        Repeater {
            model: [{text: "+", tip: qsTr("Zoom in")}, {text: "−", tip: qsTr("Zoom out")}, {text: "⤢", tip: qsTr("Show everything")}]
            Rectangle {
                id: zoomButton
                required property var modelData
                required property int index
                width: 28; height: 28; radius: 8
                color: zoomMouse.containsMouse ? Theme.hover : Theme.bg
                border.color: Theme.border
                Text { anchors.centerIn: parent; text: zoomButton.modelData.text; color: Theme.text; font.pixelSize: 15 }
                MouseArea {
                    id: zoomMouse
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (zoomButton.index === 2) graph.fit(true)
                        else {
                            const z = graph.clampZoom(graph.zoom * (zoomButton.index === 0 ? 1.4 : 1 / 1.4))
                            graph.animateView(z, graph.panX * z / graph.zoom, graph.panY * z / graph.zoom)
                        }
                    }
                }
                ToolTip.visible: zoomMouse.containsMouse
                ToolTip.delay: 500
                ToolTip.text: modelData.tip
            }
        }
    }

    // ---- Selected word: its links, each one a way to jump further ----
    Rectangle {
        id: card
        readonly property var node: graph.selected >= 0 && graph.selected < graph.network.nodes.length ? graph.network.nodes[graph.selected] : null
        readonly property var links: graph.selected >= 0 && graph.selected < graph.neighbors.length
                                     ? graph.neighbors[graph.selected].filter(function(l) { return graph.shown(l.index) }) : []
        visible: node !== null
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10
        height: cardColumn.implicitHeight + 20
        radius: 14
        color: Qt.rgba(0.067, 0.11, 0.17, 0.94)
        border.color: Theme.border
        MouseArea { anchors.fill: parent }   // clicks on the card do not reach the graph
        Column {
            id: cardColumn
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
            spacing: 8
            Row {
                spacing: 8
                width: parent.width - 24
                Rectangle { width: 10; height: 10; radius: 5; color: card.node ? graph.kindColor(card.node.kind) : "transparent"; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: card.node ? card.node.label : ""
                    color: Theme.text; font.pixelSize: 15; font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: card.node ? graph.kindName(card.node.kind) + " · " + qsTr("weight %1").arg(card.node.weight)
                                      + " · " + qsTr("links: %1").arg(card.links.length) : ""
                    color: Theme.textDim; font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Flow {
                width: parent.width
                spacing: 5
                visible: card.links.length > 0
                Repeater {
                    model: card.links.slice(0, 14)
                    Rectangle {
                        id: chip
                        required property var modelData
                        readonly property var target: graph.network.nodes[modelData.index]
                        width: chipText.implicitWidth + 20; height: 24; radius: 12
                        color: chipMouse.containsMouse ? Theme.hover : Theme.bg
                        border.color: Qt.darker(graph.kindColor(target.kind), 1.6)
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: chip.target.label + (chip.modelData.weight > 1 ? "  ×" + chip.modelData.weight : "")
                            color: Theme.text; font.pixelSize: 11
                        }
                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onEntered: graph.hovered = chip.modelData.index
                            onExited: if (graph.hovered === chip.modelData.index) graph.hovered = -1
                            onClicked: { graph.selected = chip.modelData.index; graph.centerOn(chip.modelData.index, false) }
                        }
                    }
                }
                Text {
                    visible: card.links.length > 14
                    text: qsTr("+%1 more").arg(card.links.length - 14)
                    color: Theme.textDim; font.pixelSize: 11; height: 24; verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                visible: card.links.length === 0
                text: qsTr("No links yet: this word has not come up together with others.")
                color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width
            }
        }
        Text {
            anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
            text: "×"; color: closeMouse.containsMouse ? Theme.text : Theme.textDim; font.pixelSize: 18
            width: 22; height: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: graph.selected = -1 }
        }
    }
    Text {
        id: hint
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 14
        visible: !card.visible && graph.network.nodes.length > 0
        color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
        text: qsTr("Click a word to see its links · drag to move · wheel to zoom · double-click to fit")
    }
}
