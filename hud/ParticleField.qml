import QtQuick

// Particles around the core, computed on the GPU by one shader (particles.frag):
//  * orbiting dust in two bands, each grain living a few seconds;
//  * an inflow drawn into the core while Jarvis listens or works;
//  * bursts thrown out of the core on events (memory written, a notice).
// Nothing is simulated on the CPU and no geometry is uploaded: grains follow from
// the HUD clock, so they move at the HUD's own frame rate and cost nothing while
// the HUD is still. Low quality turns them off.
Item {
    id: field
    required property var ctl
    property bool active: !ctl.low
    readonly property real size: Math.min(width, height)

    function burst(count) {
        if (!active) return
        effect.burstSeed = Math.random() * 100
        effect.burstCount = Math.max(6, Math.min(72, count))
        effect.burstAge = 0
    }

    ShaderEffect {
        id: effect
        // The inflow starts outside the core's square.
        readonly property real scaleUp: 1.25
        anchors.centerIn: parent
        width: field.size * scaleUp
        height: width
        visible: field.active

        property real time: field.ctl.time
        property real pixel: 2 / Math.max(1, field.size)
        property real scale: scaleUp
        property real orbit: 0
        property real inflow: 0
        // Grains appear with the state's pace; below 0.35 (idle) there is no inflow.
        property real inflowRate: Math.max(0, (field.ctl.particleRate - 0.35) / 0.65)
        property real density: field.ctl.cinematic ? 1.6 : 1.0
        property real burstAge: -1
        property real burstSeed: 0
        property real burstCount: 30
        property color color: field.ctl.colorA
        fragmentShader: "shaders/particles.frag.qsb"

        Connections {
            target: field.ctl
            enabled: effect.visible
            function onTick(dt) {
                // The dust's frame turns 6° a second at the state's pace.
                effect.orbit = (effect.orbit + dt * field.ctl.speed / 60) % 1
                if (effect.inflowRate > 0)
                    effect.inflow = (effect.inflow + dt * 0.6 * field.ctl.speed) % 1000
                if (effect.burstAge >= 0)
                    effect.burstAge = effect.burstAge + dt > 1.8 ? -1 : effect.burstAge + dt
            }
        }
    }
}
