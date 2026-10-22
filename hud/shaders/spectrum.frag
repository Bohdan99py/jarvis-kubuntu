#version 440
// Radial bars around the core, shaped by the measured sound: the voice's
// bands while Jarvis speaks, the microphone level while it listens.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float pixel;
    float innerRadius;
    float maxLength;
    float bars;
    float turn;
    vec4 bands;         // low, mid, high, microphone
    vec4 historyA;      // the last levels, newest first
    vec4 historyB;
    vec4 color;
    float intensity;
};

float hash(float n) { return fract(sin(n * 12.9898) * 43758.5453); }

void main() {
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    float r = length(uv);
    float ang = fract(atan(uv.y, uv.x) / 6.2831853 + 1.0 - turn);
    float id = floor(ang * bars);
    float within = fract(ang * bars);
    // Low frequencies on the sides, highs at the top and bottom, mirrored.
    float m = abs(fract(id / bars + 0.25) * 2.0 - 1.0);
    float v = mix(mix(bands.x, bands.y, smoothstep(0.0, 0.55, m)), bands.z, smoothstep(0.5, 1.0, m));
    // Recent history makes neighbouring bars differ, like a real analyser.
    int k = int(mod(id, 8.0));
    float hist = k < 4 ? historyA[k] : historyB[k - 4];
    v = max(v, bands.w) * (0.65 + 0.35 * hist / max(0.05, max(v, bands.w)));
    v = clamp(v * (0.75 + 0.5 * hash(id)), 0.0, 1.0);

    float len = maxLength * v;
    float barPx = pixel / max(r * 6.2831853 / bars, 1e-4);
    float bar = smoothstep(0.12, 0.12 + barPx, within) * smoothstep(0.62, 0.62 - barPx, within);
    float radial = step(innerRadius, r) * smoothstep(innerRadius + len + pixel, innerRadius + len, r);
    float tip = exp(-abs(r - innerRadius - len) / (pixel * 2.0)) * step(0.02, v);
    float fade = 1.0 - clamp((r - innerRadius) / max(maxLength, 1e-3), 0.0, 1.0) * 0.6;

    vec3 col = color.rgb * (bar * radial * fade * 0.85 + bar * tip * 0.9) * intensity;
    float alpha = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    fragColor = vec4(min(col, vec3(alpha)), alpha) * qt_Opacity;
}
