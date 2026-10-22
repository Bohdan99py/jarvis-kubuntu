#version 440
// The space behind everything: a polar grid around the core, a dot lattice,
// slow haze, a scan line, expanding energy waves and a vignette. Opaque.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    vec2 size;          // pixels
    vec2 centre;        // 0..1
    float coreRadius;   // pixels
    vec4 accent;
    vec4 alertColor;
    float alertMix;
    float energy;
    vec4 waves;         // ages of up to four energy waves, seconds (< 0: none)
    float detail;
    float scan;
};

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float wave(float age, float d) {
    if (age < 0.0) return 0.0;
    float radius = coreRadius + age * 380.0;
    return exp(-abs(d - radius) / (4.0 + age * 18.0)) * exp(-age * 1.3);
}

void main() {
    vec2 px = qt_TexCoord0 * size;
    vec2 c = centre * size;
    vec2 rel = px - c;
    float d = length(rel);
    float ang = atan(rel.y, rel.x);
    vec3 acc = mix(accent.rgb, alertColor.rgb, alertMix * 0.7);

    vec3 col = mix(vec3(0.020, 0.035, 0.065), vec3(0.008, 0.012, 0.024), clamp(d / (max(size.x, size.y) * 0.75), 0.0, 1.0));
    // Polar grid: rings every 56 px and spokes every 10 degrees, fading with distance.
    float near = exp(-max(d - coreRadius, 0.0) / (max(size.x, size.y) * 0.35));
    float ringLine = smoothstep(1.2, 0.0, abs(mod(d - coreRadius, 56.0) - 28.0) - 27.0);
    float spokeW = abs(fract(ang / 0.17453293 + 0.5) - 0.5) * 0.17453293 * d;
    float spoke = smoothstep(1.0, 0.0, spokeW) * step(coreRadius * 1.25, d);
    col += acc * (ringLine * 0.05 + spoke * 0.025) * near * (0.6 + 0.6 * energy);
    // Dot lattice.
    vec2 cell = abs(fract(px / 26.0) - 0.5) * 26.0;
    col += acc * smoothstep(1.3, 0.3, length(cell)) * 0.05;
    // Haze.
    if (detail > 0.5)
        col += acc * (noise(px / 380.0 + vec2(time * 0.018, -time * 0.011)) - 0.4) * 0.05;
    // Scan line.
    float sy = fract(time * 0.045) * (size.y + 200.0) - 100.0;
    col += acc * exp(-abs(px.y - sy) / 1.5) * 0.06 * scan + acc * exp(-max(sy - px.y, 0.0) / 120.0) * step(px.y, sy) * 0.012 * scan;
    // Energy waves from the core.
    float w = wave(waves.x, d) + wave(waves.y, d) + wave(waves.z, d) + wave(waves.w, d);
    col += acc * w * 0.35;
    // Vignette and a faint grain.
    vec2 q = qt_TexCoord0 - 0.5;
    col *= 1.0 - dot(q, q) * 1.15;
    col += (hash(px + fract(time) * 100.0) - 0.5) * 0.012 * detail;
    fragColor = vec4(max(col, 0.0), 1.0) * qt_Opacity;
}
