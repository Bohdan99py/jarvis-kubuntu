import QtQuick

// The machinery around the core: rings that each move in their own way.
//
//  * drift  — turns smoothly; the speed wanders on two incommensurate sines,
//             so the motion never repeats exactly;
//  * step   — a mechanical ring: it jumps one segment at a time and rests;
//  * scan   — sweeps back and forth over an arc;
//  * still  — a reference ring with ticks that only creeps.
// Rings with `rebuild` regroup their segments every few seconds (shrink, swap,
// grow), impulses send a light along some of them, warning/error recolour and
// shake them. One shader (ring.frag) draws every ring.
Item {
    id: rings
    required property var ctl
    // Mark VII adds two rings of its own (not in Low).
    property int count: ctl.low ? 3 : (ctl.cinematic ? 8 : 7) + (JarvisTheme.mark7 ? 2 : 0)

    readonly property var specs: [
        // radius, thickness, segments, duty, dim, ticks, tickLength, motion, speed, arc, rebuild, glitchy, pulses, intensity, alert
        { r: 0.40, w: 0.016, seg: 36,  duty: 0.82, dim: 0.35, ticks: 72,  tl: 0.014, motion: "drift", speed: -0.022, arc: 1.0,  rebuild: false, pulses: true,  i: 0.85, alert: 0.4 },
        { r: 0.52, w: 0.034, seg: 6,   duty: 0.88, dim: 0.0,  ticks: 0,   tl: 0,     motion: "step",  speed: 0.6,    arc: 0.72, rebuild: true,  pulses: true,  i: 0.75, alert: 1.0 },
        { r: 0.68, w: 0.014, seg: 64,  duty: 0.6,  dim: 0.5,  ticks: 0,   tl: 0,     motion: "drift", speed: 0.018,  arc: 1.0,  rebuild: true,  pulses: false, i: 0.7,  alert: 1.0 },
        { r: 0.355, w: 0.006, seg: 120, duty: 0.5, dim: 0.1,  ticks: 0,   tl: 0,     motion: "drift", speed: 0.045,  arc: 1.0,  rebuild: false, pulses: false, i: 0.6,  alert: 0.3 },
        { r: 0.60, w: 0.004, seg: 1,   duty: 1.0,  dim: 0.0,  ticks: 180, tl: 0.011, motion: "still", speed: 0.003,  arc: 1.0,  rebuild: false, pulses: true,  i: 0.45, alert: 0.2 },
        { r: 0.78, w: 0.010, seg: 3,   duty: 0.55, dim: 0.0,  ticks: 0,   tl: 0,     motion: "scan",  speed: 0.05,   arc: 1.0,  rebuild: true,  pulses: true,  i: 0.6,  alert: 1.0 },
        { r: 0.90, w: 0.002, seg: 1,   duty: 1.0,  dim: 0.0,  ticks: 360, tl: 0.012, motion: "still", speed: -0.002, arc: 1.0,  rebuild: false, pulses: false, i: 0.35, alert: 0.2 },
        { r: 0.46, w: 0.022, seg: 4,   duty: 0.2,  dim: 0.0,  ticks: 0,   tl: 0,     motion: "drift", speed: -0.07,  arc: 1.0,  rebuild: true,  pulses: false, i: 0.9,  alert: 0.6 },
        // Mark VII only.
        { r: 0.63, w: 0.009, seg: 12,  duty: 0.7,  dim: 0.0,  ticks: 0,   tl: 0,     motion: "step",  speed: -0.4,   arc: 0.86, rebuild: true,  pulses: true,  i: 0.7,  alert: 1.0 },
        { r: 0.85, w: 0.006, seg: 24,  duty: 0.45, dim: 0.2,  ticks: 48,  tl: 0.01,  motion: "drift", speed: 0.012,  arc: 1.0,  rebuild: false, pulses: true,  i: 0.5,  alert: 0.5 }
    ]
    // Low quality keeps the three that read best.
    readonly property var order: ctl.low ? [0, 1, 2] : ctl.cinematic ? [0, 1, 2, 3, 4, 5, 6, 7, 8, 9] : [0, 1, 2, 3, 4, 5, 6, 8, 9]

    Repeater {
        model: rings.count
        delegate: ShaderEffect {
            id: ring
            required property int index
            // While the quality changes, the count and the order update one after the other: never undefined.
            readonly property var spec: rings.specs[rings.order[index]] || rings.specs[0]
            anchors.fill: parent

            property real time: rings.ctl.time
            property real pixel: 2 / Math.max(1, width)
            // Listening opens the machinery a little.
            property real radius: spec.r * (1 + 0.05 * rings.ctl.openness * (index % 2 ? 1 : -0.6))
            property real thickness: spec.w * (1 + 0.6 * rings.ctl.pulse * (spec.pulses ? 1 : 0))
            property real segments: spec.seg
            property real duty: spec.duty
            property real turn: Math.random()
            property real ticks: spec.ticks
            property real tickLength: spec.tl
            property real tickTurn: 0
            property real arcStart: 0
            property real arcSpan: spec.arc
            property real pulseAngle: Math.random()
            property real pulseStrength: 0
            property real glitch: rings.ctl.instability
            property real intensity: spec.i * (0.75 + 0.45 * rings.ctl.energy) + (index < 2 ? 0.4 * rings.ctl.sLevel : 0)
            property real dim: spec.dim
            property real seed: (rings.order[index] || 0) * 3.7 + 1
            property color color: rings.ctl.colorA
            property color alertColor: rings.ctl.alertColor
            property real alertMix: rings.ctl.alertMix * spec.alert

            // Behaviour state.
            property real phase: Math.random() * 100
            property real rest: 0.5 + Math.random() * 2       // step rings: time until the next step
            property real stepFrom: 0
            property real stepTo: 0
            property real stepT: 1
            property real nextRebuild: 3 + Math.random() * 6
            property real rebuildT: -1
            property real targetDuty: spec.duty
            property real targetSegments: spec.seg
            property real targetArc: spec.arc
            property real scanDir: 1

            fragmentShader: "shaders/ring.frag.qsb"

            function advance(dt) {
                const c = rings.ctl
                const s = spec.speed * c.speed
                // Wandering speed: two sines whose periods do not divide each other.
                const wander = 1 + 0.45 * Math.sin(c.time * 0.31 + phase) * Math.sin(c.time * 0.173 + phase * 1.7)
                if (spec.motion === "drift") {
                    turn = (turn + s * wander * dt + 1) % 1
                } else if (spec.motion === "still") {
                    turn = (turn + s * dt + 1) % 1
                    tickTurn = (tickTurn - s * 0.5 * dt + 1) % 1
                } else if (spec.motion === "scan") {
                    arcStart += scanDir * s * wander * dt
                    if (arcStart > 0.18) scanDir = -1
                    if (arcStart < -0.18) scanDir = 1
                    turn = (turn + 0.004 * c.speed * dt + 1) % 1
                } else if (spec.motion === "step") {
                    // Mechanical: a quick eased jump of one segment, then a rest.
                    if (stepT < 1) {
                        stepT = Math.min(1, stepT + dt * 3.2 * spec.speed * c.speed)
                        const e = stepT < 0.5 ? 4 * stepT * stepT * stepT : 1 - Math.pow(-2 * stepT + 2, 3) / 2
                        turn = (stepFrom + (stepTo - stepFrom) * e + 1) % 1
                    } else {
                        rest -= dt * c.speed
                        if (rest <= 0) {
                            stepFrom = turn
                            stepTo = turn + (Math.random() < 0.8 ? 1 : -1) / Math.max(1, segments) * (Math.random() < 0.25 ? 2 : 1)
                            stepT = 0
                            rest = 0.6 + Math.random() * 2.4
                        }
                    }
                }
                // The light that runs along the ring.
                if (spec.pulses) {
                    pulseAngle = (pulseAngle + dt * (0.35 + 0.5 * c.energy) * (index % 2 ? -1 : 1) + 1) % 1
                    pulseStrength = Math.max(0, pulseStrength - dt * 1.8)
                }
                // Regrouping: shrink the segments, change their number, grow them back.
                if (spec.rebuild) {
                    nextRebuild -= dt * (0.6 + c.energy)
                    if (nextRebuild <= 0 && rebuildT < 0) {
                        rebuildT = 0
                        const choices = spec.seg <= 6 ? [3, 4, 5, 6, 8] : [24, 32, 48, 64, 96]
                        targetSegments = choices[Math.floor(Math.random() * choices.length)]
                        targetDuty = Math.min(0.92, spec.duty * (0.7 + Math.random() * 0.5))
                        targetArc = spec.arc >= 1 ? 1 : 0.45 + Math.random() * 0.5
                        nextRebuild = 4 + Math.random() * 8
                    }
                    if (rebuildT >= 0) {
                        rebuildT += dt * 2.2
                        if (rebuildT < 0.45) {
                            duty = Math.max(0.04, duty - dt * 4)
                        } else {
                            segments = targetSegments
                            duty = Math.min(targetDuty, duty + dt * 2.4)
                            arcSpan += (targetArc - arcSpan) * Math.min(1, dt * 4)
                            if (rebuildT > 1.4) rebuildT = -1
                        }
                    }
                }
            }
            Connections {
                target: rings.ctl
                function onTick(dt) { ring.advance(dt) }
                function onImpulse(strength) {
                    if (ring.spec.pulses && Math.random() < 0.6) {
                        ring.pulseStrength = Math.max(ring.pulseStrength, strength * 1.4)
                        ring.pulseAngle = Math.random()
                    }
                    if (ring.spec.motion === "step" && strength > 0.7) ring.rest = 0
                }
            }
        }
    }
}
