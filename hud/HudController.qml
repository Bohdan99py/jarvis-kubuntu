import QtQuick

// The HUD's one clock and its living parameters.
//
// Every effect reads `time` and the smoothed parameters below instead of running
// its own endless animation: a state change moves the targets and the values glide
// there (exponential smoothing per frame), so nothing restarts or jumps.
//
// Cost control: the whole window is redrawn whenever any of these values change,
// so the clock's rate is the HUD's frame rate: Low 20 Hz, Balanced 30 Hz while
// idle and 60 Hz when active, Cinematic 60 Hz — never the display's own rate
// (a 144 Hz panel would otherwise redraw every layer 144 times a second).
// When `running` is false — the window is hidden — nothing ticks.
Item {
    id: ctl

    property bool running: true
    property string quality: "balanced"   // low, balanced, cinematic
    property string hudState: "idle"
    readonly property bool cinematic: quality === "cinematic"
    readonly property bool low: quality === "low"
    // 0 Low, 1 Balanced, 2 Cinematic: what the shaders may spend per pixel
    // (Cinematic adds the plasma's domain warp and the background haze).
    readonly property real detail: low ? 0 : cinematic ? 2 : 1
    readonly property bool throttled: low || (!cinematic && hudState === "idle" && pulse < 0.05)
    // A ceiling the host may set: the desktop core idles at a few frames a second.
    property int maxRate: 60
    // Ticks per second.
    readonly property int rate: Math.max(1, Math.min(maxRate, low ? 20 : throttled ? 30 : 60))
    // The display's refresh rate (GraphicsInfo.refreshRate; QML's Screen has none).
    property real refreshRate: 0
    // At the display's own rate the clock follows vsync (smoothest); above the cap
    // a timer paces it, so the frames in between are not rendered at all.
    readonly property bool onVsync: refreshRate > 0 && refreshRate <= rate * 1.15

    // Measured inputs (real telemetry).
    property real level: 0          // the voice being played
    property var bands: [0, 0, 0]
    property bool levelMeasured: false
    property real micLevel: 0
    property real streamRate: 0     // characters per second of the answer
    property bool streaming: false

    property real time: 0
    property real dt: 1 / 60

    // Smoothed state parameters.
    property real energy: 0.15
    property real openness: 0
    property real turbulence: 0.15
    property real speed: 1
    property real alertMix: 0
    property real instability: 0
    property real streamIntensity: 0
    property real scan: 0.5
    property real particleRate: 0.2
    property real pulse: 0
    property real cr: 0.31; property real cg: 0.85; property real cb: 1.0
    readonly property color colorA: Qt.rgba(cr, cg, cb, 1)
    property color colorB: JarvisTheme.deep
    property color alertColor: JarvisTheme.amber
    // Smoothed sound.
    property real sLevel: 0
    property real sLow: 0
    property real sMid: 0
    property real sHigh: 0
    property real sMic: 0
    property var history: [0, 0, 0, 0, 0, 0, 0, 0]
    // Ages of energy waves (seconds), -1 = none.
    property var waves: [-1, -1, -1, -1]

    // A short irregular impulse: rings flash, a wave leaves the core.
    signal impulse(real strength)
    signal tick(real dt)

    readonly property var targets: ({
        idle:         { energy: 0.15, openness: 0.0, turbulence: 0.15, speed: 1.0, alert: 0, instability: 0, scan: 0.45, particles: 0.2, rate: 0.22 },
        listening:    { energy: 0.45, openness: 1.0, turbulence: 0.3,  speed: 1.4, alert: 0, instability: 0, scan: 0.8,  particles: 0.75, rate: 0.7 },
        processing:   { energy: 0.8,  openness: 0.25, turbulence: 0.55, speed: 2.8, alert: 0, instability: 0, scan: 1.0, particles: 1.0, rate: 1.6 },
        speaking:     { energy: 0.65, openness: 0.4, turbulence: 0.35, speed: 1.8, alert: 0, instability: 0, scan: 0.7,  particles: 0.6, rate: 0.5 },
        notification: { energy: 0.55, openness: 0.3, turbulence: 0.25, speed: 1.5, alert: 0, instability: 0, scan: 0.8,  particles: 0.5, rate: 0.6 },
        warning:      { energy: 0.6,  openness: 0.1, turbulence: 0.5,  speed: 1.2, alert: 1, instability: 0.05, scan: 0.9, particles: 0.4, rate: 0.9 },
        error:        { energy: 0.85, openness: 0.0, turbulence: 0.9,  speed: 0.6, alert: 1, instability: 1, scan: 1.0,  particles: 0.2, rate: 1.4 }
    })

    property real nextImpulse: 1.5

    // Exponential smoothing that settles: once within 0.001 the value is the target
    // and stops changing, so nothing bound to it (the cached background, colours)
    // is redrawn for differences nobody can see.
    function approach(value, target, rate, dt) {
        if (Math.abs(target - value) < 0.001) return target
        return value + (target - value) * (1 - Math.exp(-rate * dt))
    }

    function addWave() {
        const w = waves.slice()
        let slot = 0
        for (let i = 0; i < 4; ++i) {
            if (w[i] < 0) { slot = i; break }
            if (w[i] > w[slot]) slot = i
        }
        w[slot] = 0
        waves = w
    }

    function fire(strength) {
        pulse = Math.max(pulse, strength)
        if (strength > 0.55 && !low) addWave()
        impulse(strength)
    }

    function advance(step) {
        const t = targets[hudState] || targets.idle
        dt = step
        time += step * JarvisTheme.motion
        // States glide into each other.
        const k = hudState === "error" ? 9 : 3.2
        energy = approach(energy, t.energy + 0.25 * sLevel, k, step)
        openness = approach(openness, t.openness + 0.4 * sMic, k, step)
        turbulence = approach(turbulence, t.turbulence + 0.3 * sHigh, k, step)
        speed = approach(speed, t.speed * (1 + 0.6 * Math.min(1, streamRate / 900)), 2.2, step)
        alertMix = approach(alertMix, t.alert, 4, step)
        instability = approach(instability, t.instability, hudState === "error" ? 14 : 2.5, step)
        scan = approach(scan, t.scan, 2, step)
        particleRate = approach(particleRate, t.particles, 2, step)
        streamIntensity = approach(streamIntensity, (hudState === "processing" ? 0.55 : 0) + (streaming ? 0.6 : 0), 2.5, step)
        const c = Qt.color(JarvisTheme.stateColor(hudState))
        cr = approach(cr, c.r, 3, step); cg = approach(cg, c.g, 3, step); cb = approach(cb, c.b, 3, step)
        const deep = JarvisTheme.stateDeep(hudState)
        if (!Qt.colorEqual(colorB, deep)) colorB = deep
        const alert = hudState === "error" ? JarvisTheme.red : JarvisTheme.amber
        if (!Qt.colorEqual(alertColor, alert)) alertColor = alert
        // Sound: fast attack, slower release, like a VU meter.
        const b = bands || [0, 0, 0]
        sLevel = approach(sLevel, level, level > sLevel ? 22 : 7, step)
        sLow = approach(sLow, b[0] || 0, (b[0] || 0) > sLow ? 22 : 6, step)
        sMid = approach(sMid, b[1] || 0, (b[1] || 0) > sMid ? 22 : 6, step)
        sHigh = approach(sHigh, b[2] || 0, (b[2] || 0) > sHigh ? 25 : 8, step)
        sMic = approach(sMic, micLevel, micLevel > sMic ? 20 : 6, step)
        // History of the sound for the analyser ring, 12 samples a second.
        if (Math.floor(time * 12) !== Math.floor((time - step) * 12)) {
            const h = history.slice(0, 7)
            h.unshift(Math.max(sLevel, sMic))
            history = h
        }
        // A loud syllable is an impulse.
        if (levelMeasured && level - sLevel > 0.18 && pulse < 0.4)
            fire(0.5 + level * 0.5)
        // Irregular impulses: a Poisson process whose rate follows the state.
        nextImpulse -= step
        if (nextImpulse <= 0) {
            fire(0.35 + Math.random() * 0.65)
            nextImpulse = -Math.log(1 - Math.random()) / Math.max(0.05, t.rate)
        }
        pulse = approach(pulse, 0, 3.5, step)
        // Waves grow old and vanish.
        if (waves.some(w => w >= 0))
            waves = waves.map(w => w < 0 ? -1 : (w + step > 3.2 ? -1 : w + step))
        tick(step)
    }

    FrameAnimation {
        // Also capped here: where vsync does not block (some drivers, virtual
        // displays) frames would otherwise follow each other as fast as possible.
        property real pending: 0
        running: ctl.running && ctl.onVsync
        onTriggered: {
            pending += frameTime
            if (pending < 0.8 / ctl.rate) return
            ctl.advance(Math.min(pending, 0.1))
            pending = 0
        }
    }
    Timer {
        property real last: 0
        interval: Math.round(1000 / ctl.rate)
        repeat: true
        running: ctl.running && !ctl.onVsync
        onRunningChanged: last = Date.now()
        onTriggered: {
            const now = Date.now()
            ctl.advance(Math.min(Math.max(now - last, 1) / 1000, 0.1))
            last = now
        }
    }
}
