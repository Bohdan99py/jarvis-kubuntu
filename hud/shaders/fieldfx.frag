#version 440
// The space behind everything, the moving part: slow haze and the energy waves the
// core sends out. Everything here is soft, so the item renders it at a fraction of
// the window's resolution and scales it up. Additive: alpha stays 0.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    vec2 size;          // pixels of the window (not of the reduced texture)
    vec2 centre;        // 0..1
    float coreRadius;   // pixels
    vec4 accent;
    vec4 alertColor;
    float alertMix;
    float energy;
    vec4 waves;         // ages of up to four energy waves, seconds (< 0: none)
    float detail;
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
    float d = length(px - centre * size);
    vec3 acc = mix(accent.rgb, alertColor.rgb, alertMix * 0.7);
    vec3 col = vec3(0.0);
    // Haze, brighter with activity; Cinematic only.
    if (detail > 1.5)
        col += acc * max(noise(px / 380.0 + vec2(time * 0.018, -time * 0.011)) - 0.35, 0.0) * (0.05 + 0.04 * energy);
    // Energy waves from the core.
    float w = wave(waves.x, d) + wave(waves.y, d) + wave(waves.z, d) + wave(waves.w, d);
    col += acc * w * 0.35;
    fragColor = vec4(col * qt_Opacity, 0.0);
}
