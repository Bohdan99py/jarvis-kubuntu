import QtQuick

// A holographic panel. It unfolds in phases — a point travels from `origin`
// to its place and draws a line, the line opens into a frame, then the content
// fades in — and folds back the same way. Hover brightens it, a click on the
// title shows the details, the title bar drags it (a double click returns it to
// its place), transient panels hide by themselves after `autoHide` ms unless
// hovered or pinned. HUDPanelManager places it.
Item {
    id: panel

    property string title
    property string tag
    property color accent: JarvisTheme.cyan
    // What the HUD wants; the manager decides whether there is room (`shown`).
    property bool wantShown: true
    property bool shown: false
    property bool expanded: false
    property bool pinned: false
    property bool busy: false            // a scan runs down the frame
    property int autoHide: 0             // ms; 0 = stays
    property bool movable: true          // the title bar drags it (not inside a Plasma widget)
    property string column: "left"       // left or right
    property int order: 0
    property point origin: Qt.point(slotX, slotY)
    default property alias content: body.data
    property alias details: extra.data
    readonly property bool hovered: hover.hovered

    // Set by the manager.
    property real slotX: 0
    property real slotY: 0
    property real panelWidth: 260
    property bool dragged: false
    readonly property real fullHeight: header.height + body.childrenRect.height + 20
                                       + (expanded && extra.children.length > 0 ? extra.childrenRect.height + 8 : 0)
    readonly property real layoutHeight: fullHeight * Math.min(1, reveal * 1.6)

    property real reveal: 0
    readonly property real travel: Math.min(1, reveal / 0.3)
    readonly property real draw: Math.min(1, Math.max(0, (reveal - 0.2) / 0.3))
    readonly property real open: Math.min(1, Math.max(0, (reveal - 0.45) / 0.35))
    readonly property real fade: Math.min(1, Math.max(0, (reveal - 0.7) / 0.3))
    function ease(v) { return 1 - Math.pow(1 - v, 3) }

    // Brings a transient panel up (again) and keeps it a while (new data arrived).
    function touch() { wantShown = true; if (autoHide > 0) hideTimer.restart() }

    width: panelWidth
    height: Math.max(2, fullHeight * ease(open))
    // Only the details unfolding glide; new numbers that change the height do not
    // start an animation (they arrive every few seconds).
    property bool unfolding: false
    onExpandedChanged: { unfolding = true; unfoldEnd.restart() }
    Timer { id: unfoldEnd; interval: 300 * JarvisTheme.motion; onTriggered: panel.unfolding = false }
    Behavior on height { enabled: panel.reveal >= 1 && panel.unfolding; NumberAnimation { duration: 240 * JarvisTheme.motion; easing.type: Easing.OutCubic } }
    visible: reveal > 0.001
    function placeX() { return origin.x + (slotX - origin.x) * ease(travel) }
    function placeY() { return origin.y + (slotY - origin.y) * ease(travel) }
    x: placeX()
    y: placeY()
    // Dragging replaces these bindings; a double click on the title restores them.
    onDraggedChanged: if (!dragged) { x = Qt.binding(placeX); y = Qt.binding(placeY) }
    Behavior on slotY { enabled: panel.reveal >= 1; NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    Behavior on slotX { enabled: panel.reveal >= 1; NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

    NumberAnimation on reveal {
        id: showAnim
        running: false
        to: 1
        duration: 760
        easing.type: Easing.Linear
    }
    NumberAnimation on reveal {
        id: hideAnim
        running: false
        to: 0
        duration: 480
        easing.type: Easing.Linear
    }
    onShownChanged: {
        if (shown) { hideAnim.stop(); showAnim.restart(); if (autoHide > 0) hideTimer.restart() }
        else { showAnim.stop(); hideAnim.restart() }
    }
    Component.onCompleted: if (shown) showAnim.start()

    Timer {
        id: hideTimer
        interval: Math.max(1, panel.autoHide)
        onTriggered: {
            if (panel.hovered || panel.pinned) restart()
            else panel.wantShown = false
        }
    }

    // The travelling point and the line it draws.
    Rectangle {
        visible: panel.open < 1
        y: -1
        width: Math.max(4, panel.width * panel.ease(panel.draw))
        height: 2
        color: panel.accent
        opacity: 0.9
        Rectangle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 6; height: 6; radius: 3; color: "white" }
    }

    // Frame (0.19.3: rounded, the shared surface tokens — never so thin that text
    // stops reading on a light wallpaper — and a dark outline for light backgrounds).
    Rectangle {
        id: frame
        anchors.fill: parent
        opacity: panel.ease(panel.open)
        radius: JarvisTheme.radius
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(Math.min(1, JarvisTheme.surface.r * 1.5), Math.min(1, JarvisTheme.surface.g * 1.4), Math.min(1, JarvisTheme.surface.b * 1.35), JarvisTheme.surfaceAlpha) }
            GradientStop { position: 1; color: JarvisTheme.surface }
        }
        border.width: 1
        border.color: panel.hovered ? Qt.rgba(panel.accent.r, panel.accent.g, panel.accent.b, 0.85)
                                    : Qt.rgba(panel.accent.r, panel.accent.g, panel.accent.b, JarvisTheme.lightBackdrop ? 0.5 : 0.32)
        Behavior on border.color { ColorAnimation { duration: 160 } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"; border.color: JarvisTheme.outline; z: -1
        }
        // Outer glow on hover.
        Rectangle {
            anchors.fill: parent; anchors.margins: -4
            radius: parent.radius + 4
            color: "transparent"
            border.width: 3
            border.color: Qt.rgba(panel.accent.r, panel.accent.g, panel.accent.b, panel.hovered ? 0.16 * JarvisTheme.glow : 0)
            Behavior on border.color { ColorAnimation { duration: 160 } }
        }
        // Corner brackets slide in last, inside the rounded corners.
        Repeater {
            model: 4
            delegate: Item {
                required property int index
                id: bracket
                readonly property bool atRight: index % 2 === 1
                readonly property bool atBottom: index > 1
                width: 10; height: 10
                opacity: Math.min(1, JarvisTheme.glow)
                x: atRight ? frame.width - width - 4 + (1 - panel.fade) * 6 : 4 - (1 - panel.fade) * 6
                y: atBottom ? frame.height - height - 4 : 4
                Rectangle { x: bracket.atRight ? bracket.width - 2 : 0; width: 2; height: bracket.height; radius: 1; color: panel.accent }
                Rectangle { y: bracket.atBottom ? bracket.height - 2 : 0; width: bracket.width; height: 2; radius: 1; color: panel.accent }
            }
        }
        // A scan line runs down while the panel's subject is working.
        Rectangle {
            visible: panel.busy && panel.fade > 0.9
            width: parent.width - 2; x: 1
            height: 18
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(panel.accent.r, panel.accent.g, panel.accent.b, 0.18) }
            }
            NumberAnimation on y { running: panel.busy && panel.visible; loops: Animation.Infinite; from: 0; to: Math.max(0, frame.height - 18); duration: 1600 }
        }
    }

    // Title bar: click for details, drag to move, double click to put back.
    Item {
        id: header
        width: parent.width
        height: 30
        opacity: panel.fade
        Rectangle { anchors.bottom: parent.bottom; x: 12; width: parent.width - 24; height: 1; color: Qt.rgba(panel.accent.r, panel.accent.g, panel.accent.b, 0.25) }
        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - tagText.width - 34
            // The title types itself in.
            text: panel.title.substring(0, Math.ceil(panel.title.length * Math.min(1, panel.fade * 1.4)))
            color: panel.accent
            font.family: JarvisTheme.mono
            font.pixelSize: JarvisTheme.fontTitle
            font.bold: true
            font.letterSpacing: 2
            elide: Text.ElideRight
        }
        Text {
            id: tagText
            anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width * 0.5)
            elide: Text.ElideRight
            text: (panel.pinned ? "◆ " : "") + panel.tag + (extra.children.length > 0 ? (panel.expanded ? "  ▴" : "  ▾") : "")
            color: JarvisTheme.textSoft
            font.family: JarvisTheme.mono
            font.pixelSize: JarvisTheme.fontSmall
        }
        TapHandler {
            onTapped: panel.expanded = !panel.expanded
            onDoubleTapped: panel.dragged = false
        }
        TapHandler {
            enabled: panel.movable
            acceptedButtons: Qt.RightButton
            onTapped: panel.pinned = !panel.pinned
        }
        DragHandler {
            enabled: panel.movable
            target: panel
            xAxis.minimum: 0
            xAxis.maximum: panel.parent ? panel.parent.width - panel.width : 0
            yAxis.minimum: 0
            yAxis.maximum: panel.parent ? panel.parent.height - 30 : 0
            onActiveChanged: if (active) panel.dragged = true
        }
    }

    Item {
        id: body
        x: 14
        y: header.height + 8
        width: parent.width - 28
        height: childrenRect.height
        opacity: panel.fade
        clip: panel.open < 1
    }
    // The details unfold: the frame grows (below) and they fade in.
    Item {
        id: extra
        x: 14
        y: body.y + body.height + 6
        width: parent.width - 28
        height: childrenRect.height
        visible: opacity > 0.01
        opacity: panel.expanded && panel.fade > 0.5 ? panel.fade : 0
        Behavior on opacity { NumberAnimation { duration: 220 * JarvisTheme.motion; easing.type: Easing.OutCubic } }
    }
    HoverHandler { id: hover }
}
