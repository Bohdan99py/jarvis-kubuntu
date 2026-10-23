#version 440
// The space behind everything, the still part: a polar grid around the core, a
// dot lattice, a faint grain and a vignette. Opaque. It has no clock: the item
// caches it in a layer, so it is drawn again only when the colour or size changes.
// The moving part (haze, energy waves) is fieldfx.frag.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;          // pixels
    vec2 centre;        // 0..1
    float coreRadius;   // pixels
    vec4 accent;
    vec4 alertColor;
    float alertMix;
    float detail;
};

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

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
    col += acc * (ringLine * 0.05 + spoke * 0.025) * near * 0.75;
    // Dot lattice.
    vec2 cell = abs(fract(px / 26.0) - 0.5) * 26.0;
    col += acc * smoothstep(1.3, 0.3, length(cell)) * 0.05;
    // Vignette and a faint, still grain.
    vec2 q = qt_TexCoord0 - 0.5;
    col *= 1.0 - dot(q, q) * 1.15;
    col += (hash(px) - 0.5) * 0.012 * min(detail, 1.0);
    fragColor = vec4(max(col, 0.0), 1.0) * qt_Opacity;
}
