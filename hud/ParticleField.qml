import QtQuick
import QtQuick.Particles

// Particles around the core, simulated by Qt Quick Particles and drawn as one
// batched sprite layer on the GPU:
//  * an orbiting dust ring — emitted at rest inside a frame that turns with the
//    HUD clock, so the dust orbits without per-particle scripting;
//  * an inflow pulled towards the core while Jarvis listens or works;
//  * bursts thrown out of the core on events (memory written, a notice).
// Low quality turns them off; idle Balanced pauses the simulation.
Item {
    id: field
    required property var ctl
    property bool active: !ctl.low
    readonly property real size: Math.min(width, height)

    function burst(count) {
        if (active) bursts.burst(count)
    }

    ParticleSystem {
        id: system
        running: field.active && field.visible
        paused: field.ctl.throttled && !field.ctl.cinematic
    }

    Item {
        id: orbit
        anchors.centerIn: parent
        width: field.size
        height: field.size
        Connections {
            target: field.ctl
            enabled: field.active
            function onTick(dt) { orbit.rotation = (orbit.rotation + dt * 6 * field.ctl.speed) % 360 }
        }
        ImageParticle {
            system: system
            groups: ["orbit"]
            source: "glow.png"
            color: Qt.lighter(field.ctl.colorA, 1.3)
            colorVariation: 0.08
            alpha: 0.55
            entryEffect: ImageParticle.Fade
        }
        Emitter {
            system: system
            group: "orbit"
            anchors.centerIn: parent
            width: field.size * 0.86
            height: field.size * 0.86
            shape: EllipseShape { fill: false }
            emitRate: field.ctl.cinematic ? 60 : 32
            lifeSpan: 5200
            lifeSpanVariation: 2000
            size: 4
            sizeVariation: 3
            endSize: 1
            velocity: PointDirection { xVariation: 4; yVariation: 4 }
        }
        Emitter {
            system: system
            group: "orbit"
            anchors.centerIn: parent
            width: field.size * 0.58
            height: field.size * 0.58
            shape: EllipseShape { fill: false }
            emitRate: field.ctl.cinematic ? 30 : 16
            lifeSpan: 3800
            lifeSpanVariation: 1200
            size: 3
            sizeVariation: 2
            velocity: PointDirection { xVariation: 3; yVariation: 3 }
        }
    }

    // Inflow: particles drawn into the core.
    ImageParticle {
        system: system
        groups: ["inflow"]
        source: "glow.png"
        color: field.ctl.colorA
        alpha: 0.7
        entryEffect: ImageParticle.Fade
    }
    Emitter {
        id: inflow
        system: system
        group: "inflow"
        anchors.centerIn: parent
        width: field.size * 1.15
        height: field.size * 1.15
        shape: EllipseShape { fill: false }
        enabled: field.ctl.particleRate > 0.35
        emitRate: 90 * field.ctl.particleRate * (field.ctl.cinematic ? 1.5 : 1)
        lifeSpan: 1700
        size: 5
        sizeVariation: 3
        endSize: 1
        velocity: TargetDirection {
            targetItem: field
            targetX: field.width / 2
            targetY: field.height / 2
            proportionalMagnitude: true
            magnitude: 0.55
        }
    }

    // Bursts out of the core.
    ImageParticle {
        system: system
        groups: ["burst"]
        source: "glow.png"
        color: Qt.lighter(field.ctl.colorA, 1.4)
        alpha: 0.9
    }
    Emitter {
        id: bursts
        system: system
        group: "burst"
        anchors.centerIn: parent
        width: field.size * 0.3
        height: field.size * 0.3
        shape: EllipseShape {}
        enabled: false
        lifeSpan: 1300
        lifeSpanVariation: 400
        size: 6
        endSize: 0
        velocity: AngleDirection { angleVariation: 360; magnitude: field.size * 0.35; magnitudeVariation: field.size * 0.15 }
    }
}
