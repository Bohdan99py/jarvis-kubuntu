import QtQuick
import QtQuick.Shapes

// The shade under the Jarvis core (0.19.3): a soft radial darkening that
// fades to nothing before the item's edge — no frame, no rectangle — so the
// core's thin lines read on any wallpaper. Static geometry (one shape, drawn
// once); on a light wallpaper it is denser, on a dark one barely there.
Shape {
    id: shade
    property real strength: 0.6        // 0..1, the user's setting
    property bool soft: true           // a wider, gentler falloff
    readonly property real radius: Math.min(width, height) / 2 * (soft ? 1.0 : 0.86)
    readonly property real alpha: Math.min(0.92, strength * (JarvisTheme.lightBackdrop ? 0.95 : 0.6))
    readonly property color tone: JarvisTheme.minimal ? "#05070a" : JarvisTheme.mark7 ? "#0c0504" : "#020812"
    visible: strength > 0.01
    ShapePath {
        strokeColor: "transparent"
        strokeWidth: 0
        fillGradient: RadialGradient {
            centerX: shade.width / 2; centerY: shade.height / 2; centerRadius: shade.radius
            focalX: centerX; focalY: centerY; focalRadius: 0
            GradientStop { position: 0.0; color: Qt.rgba(shade.tone.r, shade.tone.g, shade.tone.b, shade.alpha) }
            GradientStop { position: shade.soft ? 0.45 : 0.55; color: Qt.rgba(shade.tone.r, shade.tone.g, shade.tone.b, shade.alpha * 0.82) }
            GradientStop { position: shade.soft ? 0.75 : 0.8; color: Qt.rgba(shade.tone.r, shade.tone.g, shade.tone.b, shade.alpha * 0.35) }
            GradientStop { position: 1.0; color: Qt.rgba(shade.tone.r, shade.tone.g, shade.tone.b, 0) }
        }
        PathAngleArc {
            centerX: shade.width / 2; centerY: shade.height / 2
            radiusX: shade.radius; radiusY: shade.radius
            startAngle: 0; sweepAngle: 360
        }
    }
}
