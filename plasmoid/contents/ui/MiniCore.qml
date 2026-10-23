import QtQuick
import QtQuick.Shapes
import "theme.js" as Theme

// The Jarvis core in miniature: rings that turn, a heart that breathes and two
// gauges (CPU outside, memory inside). The rings are static geometry drawn once
// into a layer and turned by render-thread animators: no JavaScript and no
// repainting per frame, so the widget costs next to nothing on the GPU and
// nothing on the CPU while it moves. "off" (and a covered desktop) stops it.
Item {
    id: core
    property string hudState: "idle"    // idle, processing, notification, error, offline…
    property string intensity: "normal" // off, low, normal, high
    property bool online: true
    property real cpu: -1               // percent; -1 hides the gauge
    property real memory: -1
    property bool compact: false        // panel icon: no gauges, fewer rings

    readonly property real size: Math.min(width, height)
    readonly property color tint: Theme.stateColor(online ? hudState : "offline")
    readonly property color deep: Theme.stateDeep(online ? hudState : "offline")
    readonly property bool animate: online && intensity !== "off" && visible && size > 0
    // Seconds per turn grow with a calmer intensity; work makes everything quicker.
    readonly property real pace: (intensity === "high" ? 0.6 : intensity === "low" ? 2.6 : 1) * (hudState === "processing" ? 0.45 : 1)

    function tinted(alpha) { return Qt.rgba(tint.r, tint.g, tint.b, alpha) }

    component Ring: Item {
        id: ring
        property real r: 0.4          // radius as a share of the core's size
        property real w: 2
        property real a1: 70          // three arcs, in degrees
        property real a2: 40
        property real a3: 110
        property int period: 20000    // one turn, at normal pace
        property bool ccw: false
        anchors.fill: parent
        // Drawn once with multisampling, then only turned.
        layer.enabled: true
        layer.samples: 4
        layer.smooth: true
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: core.tint
                strokeWidth: ring.w
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: ring.width / 2; centerY: ring.height / 2; radiusX: core.size * ring.r; radiusY: radiusX; startAngle: 0; sweepAngle: ring.a1 }
                PathAngleArc { centerX: ring.width / 2; centerY: ring.height / 2; radiusX: core.size * ring.r; radiusY: radiusX; startAngle: 120; sweepAngle: ring.a2 }
                PathAngleArc { centerX: ring.width / 2; centerY: ring.height / 2; radiusX: core.size * ring.r; radiusY: radiusX; startAngle: 215; sweepAngle: ring.a3 }
            }
        }
        RotationAnimator on rotation {
            id: turn
            from: ring.ccw ? 360 : 0
            to: ring.ccw ? 0 : 360
            duration: Math.max(1000, ring.period * core.pace)
            loops: Animation.Infinite
            running: core.animate
            // A new pace applies at once, not after the current turn.
            onDurationChanged: if (running) restart()
        }
    }

    component Gauge: Shape {
        id: gauge
        property real r: 0.47
        property real value: -1       // percent
        property real w: 3
        anchors.fill: parent
        visible: value >= 0 && !core.compact
        ShapePath {
            strokeColor: Qt.rgba(core.tint.r, core.tint.g, core.tint.b, 0.14)
            strokeWidth: gauge.w
            fillColor: "transparent"
            PathAngleArc { centerX: gauge.width / 2; centerY: gauge.height / 2; radiusX: core.size * gauge.r; radiusY: radiusX; startAngle: 0; sweepAngle: 360 }
        }
        ShapePath {
            strokeColor: gauge.value > 85 ? Theme.amber : core.tint
            strokeWidth: gauge.w
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: gauge.width / 2; centerY: gauge.height / 2
                radiusX: core.size * gauge.r; radiusY: radiusX
                startAngle: -90
                sweepAngle: 3.6 * Math.max(0, Math.min(100, gauge.value))
                Behavior on sweepAngle { enabled: core.animate && core.intensity !== "low"; NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            }
        }
    }

    Item {
        id: square
        anchors.centerIn: parent
        width: core.size
        height: core.size

        // Glow behind everything.
        Shape {
            anchors.fill: parent
            opacity: core.online ? 1 : 0.5
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: square.width / 2; centerY: square.height / 2
                    focalX: centerX; focalY: centerY
                    centerRadius: square.width / 2
                    GradientStop { position: 0; color: core.tinted(0.32) }
                    GradientStop { position: 0.55; color: core.tinted(0.08) }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathAngleArc { centerX: square.width / 2; centerY: square.height / 2; radiusX: square.width / 2; radiusY: radiusX; startAngle: 0; sweepAngle: 360 }
            }
        }

        Gauge { r: 0.47; value: core.cpu; w: Math.max(2, core.size * 0.022) }
        Gauge { r: 0.42; value: core.memory; w: Math.max(1.5, core.size * 0.016); opacity: 0.8 }

        Ring { r: core.compact ? 0.44 : 0.36; w: Math.max(1.5, core.size * 0.018); a1: 80; a2: 34; a3: 120; period: 18000; opacity: 0.9 }
        Ring { r: core.compact ? 0.34 : 0.3; w: Math.max(1, core.size * 0.01); a1: 150; a2: 20; a3: 60; period: 11000; ccw: true; opacity: 0.6 }
        Ring { visible: !core.compact; r: 0.24; w: Math.max(1, core.size * 0.008); a1: 24; a2: 24; a3: 24; period: 7000; opacity: 0.55 }
        // Only at the highest intensity: a fast outer tick ring.
        Ring { visible: core.intensity === "high" && !core.compact; r: 0.5; w: 1; a1: 6; a2: 6; a3: 6; period: 4200; ccw: true; opacity: 0.5 }

        // The heart.
        Rectangle {
            id: heart
            anchors.centerIn: parent
            width: core.size * (core.compact ? 0.36 : 0.26)
            height: width
            radius: width / 2
            color: core.deep
            border.color: core.tint
            border.width: Math.max(1, core.size * 0.012)
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.48
                height: width
                radius: width / 2
                color: core.online ? Theme.ice : Theme.textDim
                opacity: 0.85
            }
            SequentialAnimation {
                loops: Animation.Infinite
                running: core.animate && core.intensity !== "low"
                ScaleAnimator { target: heart; from: 1; to: 1.08; duration: 1300 * core.pace; easing.type: Easing.InOutSine }
                ScaleAnimator { target: heart; from: 1.08; to: 1; duration: 1300 * core.pace; easing.type: Easing.InOutSine }
            }
        }
    }
}
