import QtQuick
import QtQuick.Shapes

// A short history line (network, CPU): static geometry, redrawn only when a
// new sample arrives — no animation, no CPU painting.
Shape {
    id: spark
    property var values: []
    property real maximum: 0         // 0: the largest value shown
    property color color: JarvisTheme.cyan
    property real fill: 0.12
    implicitHeight: 32
    readonly property real peak: Math.max(maximum, ...(values.length ? values : [1]), 1e-9)
    function px(i) { return values.length < 2 ? 0 : i * width / (values.length - 1) }
    function py(v) { return height - 1 - Math.max(0, v) / peak * (height - 2) }
    visible: values.length > 1
    ShapePath {
        strokeColor: spark.color
        strokeWidth: 1.5
        fillColor: Qt.rgba(spark.color.r, spark.color.g, spark.color.b, spark.fill)
        startX: 0; startY: spark.height
        PathPolyline {
            path: {
                const pts = [Qt.point(0, spark.height)]
                for (let i = 0; i < spark.values.length; ++i)
                    pts.push(Qt.point(spark.px(i), spark.py(spark.values[i])))
                pts.push(Qt.point(spark.width, spark.height))
                return pts
            }
        }
    }
}
