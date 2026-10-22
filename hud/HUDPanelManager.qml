import QtQuick

// Places HolographicPanels in two columns beside the core — left and right of
// `coreRect` — stacked by `order`. Heights follow the panels as they unfold, so
// the others move along instead of overlapping. A column that runs out of room
// folds its last panels away (and brings them back when room returns). A panel
// the user dragged keeps its place until double-clicked.
Item {
    id: manager

    property rect coreRect: Qt.rect(width / 2, height / 2, 0, 0)
    property real gap: 10
    property real margin: 12
    readonly property real columnWidth: Math.max(180, Math.min(290, coreRect.x - margin * 2))
    readonly property bool roomy: coreRect.x - margin * 2 >= 180

    function panels() {
        const out = []
        for (let i = 0; i < children.length; ++i)
            if (children[i].layoutHeight !== undefined)
                out.push(children[i])
        return out
    }

    function layout() {
        const all = panels()
        for (const side of ["left", "right"]) {
            const column = all.filter(p => p.column === side).sort((a, b) => a.order - b.order)
            let y = margin
            for (const p of column) {
                p.panelWidth = columnWidth
                p.slotX = side === "left" ? margin : width - columnWidth - margin
                // Too little room: the panel waits folded (transients always get a place).
                const fits = y + p.fullHeight <= height - margin || p.autoHide > 0
                const show = p.wantShown && fits && (roomy || p.autoHide > 0)
                if (p.shown !== show) p.shown = show
                p.slotY = y
                if (p.reveal > 0 || p.shown)
                    y += p.layoutHeight + gap
            }
        }
    }

    // Relayout on the next frame after anything moves, at most once a frame.
    Timer { id: pending; interval: 0; onTriggered: manager.layout() }
    function schedule() { if (!pending.running) pending.start() }
    onWidthChanged: schedule()
    onHeightChanged: schedule()
    onCoreRectChanged: schedule()
    property var managed: []
    onChildrenChanged: {
        for (const p of panels())
            if (managed.indexOf(p) < 0) {
                managed.push(p)
                p.layoutHeightChanged.connect(schedule)
                p.wantShownChanged.connect(schedule)
                p.fullHeightChanged.connect(schedule)
            }
        schedule()
    }
}
