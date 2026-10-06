import QtQuick
import QtQuick.Controls
import Jarvis

Rectangle {
    id: graph
    color: "#0c1523"
    radius: 20
    border.color: Theme.border
    property string graphData: "{}"
    property var network: ({nodes: [], edges: [], examples: 0})
    property int selected: -1
    property real phase: 0
    onGraphDataChanged: {
        try { network = JSON.parse(graphData) } catch (e) { network = {nodes: [], edges: [], examples: 0} }
        selected = -1
        canvas.requestPaint()
    }
    function point(i) {
        const count = network.nodes.length
        const angle = i * 2.399963
        const radius = Math.sqrt((i + 1) / Math.max(1, count)) * Math.min(width * 0.37, height * 0.32)
        return {x: width / 2 + Math.cos(angle) * radius, y: height / 2 + Math.sin(angle) * radius}
    }
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onSelectedChanged: canvas.requestPaint()
    Timer {
        interval: 80; running: graph.visible && graph.network.nodes.length > 0; repeat: true
        onTriggered: { graph.phase += 0.08; canvas.requestPaint() }
    }
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const nodes = graph.network.nodes || []
            const edges = graph.network.edges || []
            edges.forEach(function(edge) {
                const a = graph.point(edge.source), b = graph.point(edge.target)
                const active = graph.selected < 0 || edge.source === graph.selected || edge.target === graph.selected
                ctx.strokeStyle = active ? "#284f65" : "#152333"
                ctx.lineWidth = Math.min(3, 0.5 + edge.weight * 0.4)
                ctx.beginPath(); ctx.moveTo(a.x,a.y); ctx.lineTo(b.x,b.y); ctx.stroke()
                if (active) {
                    const t = (graph.phase * 0.22 + edge.source * 0.13) % 1
                    ctx.fillStyle = "#55dce5"; ctx.beginPath()
                    ctx.arc(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,1.8,0,Math.PI*2); ctx.fill()
                }
            })
            nodes.forEach(function(node,i) {
                const p = graph.point(i), r = Math.min(12, 4+Math.sqrt(node.weight)*2)
                ctx.fillStyle = i === graph.selected ? "#b3a5ff" : "#50d9d4"
                ctx.shadowColor = ctx.fillStyle; ctx.shadowBlur = 12
                ctx.beginPath(); ctx.arc(p.x,p.y,r,0,Math.PI*2); ctx.fill(); ctx.shadowBlur = 0
                ctx.fillStyle = "#c0d2df"; ctx.font = "11px sans-serif"; ctx.textAlign = "center"
                ctx.fillText(node.label,p.x,p.y+r+16)
            })
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: function(mouse) {
            let nearest = -1, distance = 26
            for (let i=0; i<graph.network.nodes.length; i++) {
                const p=graph.point(i), d=Math.hypot(mouse.x-p.x,mouse.y-p.y)
                if (d<distance) { nearest=i; distance=d }
            }
            graph.selected=nearest
        }
    }
    Text {
        anchors.centerIn: parent
        visible: graph.network.nodes.length === 0
        text: "Память пока пуста\nДобавьте первый пример обучения"
        color: Theme.textDim; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.5
    }
    Text {
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 18
        color: Theme.textDim; font.pixelSize: 12
        text: graph.selected >= 0 ? graph.network.nodes[graph.selected].label + " · примеров: " + graph.network.nodes[graph.selected].weight
              : "Слова и совместные упоминания · до 48 узлов"
    }
}
