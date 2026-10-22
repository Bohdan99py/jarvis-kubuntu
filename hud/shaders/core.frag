#version 440
// The energy core: a plasma body whose surface is deformed by the real sound
// (bands), wrapped in a rim and a halo. Output is premultiplied alpha.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float energy;       // overall activity 0..1
    float openness;     // listening: the core opens up
    float turbulence;   // surface noise
    float level;        // measured output level 0..1
    vec4 bands;         // low, mid, high, microphone
    vec4 colorA;        // primary
    vec4 colorB;        // deep secondary
    vec4 alertColor;
    float alertMix;
    float instability;  // error: glitch 0..1
    float pulse;        // a recent impulse 0..1
    float detail;       // quality 0..1
};

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { v += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
    return v;
}

void main() {
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    // Error: rows of the image slip sideways for a moment.
    float row = floor(uv.y * 22.0);
    float slip = instability * step(0.55, hash(vec2(row, floor(time * 24.0))));
    uv.x += slip * (hash(vec2(floor(time * 31.0), row)) - 0.5) * 0.3;

    float r = length(uv);
    float a = atan(uv.y, uv.x);
    float t = time;

    // The surface: harmonics driven by the sound, plus slow organic noise.
    float base = 0.34 + 0.05 * energy + 0.07 * openness + 0.04 * pulse + 0.05 * level;
    float wob = (0.008 + 0.07 * bands.x) * sin(3.0 * a + t * 1.3)
              + (0.005 + 0.05 * bands.y) * sin(7.0 * a - t * 2.1 + 1.7)
              + (0.003 + 0.04 * bands.z) * sin(13.0 * a + t * 3.7 + 0.4)
              + 0.035 * bands.w * sin(5.0 * a + t * 7.0)
              + (0.018 + 0.06 * turbulence) * (fbm(vec2(a * 1.3 + t * 0.21, t * 0.33)) - 0.5);
    float surf = base + wob;
    float d = r - surf;

    // Plasma: a swirl that turns faster towards the centre.
    float swirl = a + t * 0.17 + 1.6 * (1.0 - r);
    vec2 pp = vec2(cos(swirl), sin(swirl)) * r * 3.0;
    float warp = detail > 0.5 ? fbm(uv * 2.6 - vec2(t * 0.12, -t * 0.08)) * 1.4 : 0.0;
    float plasma = fbm(pp + vec2(t * 0.25, -t * 0.19) + warp);

    float inside = smoothstep(0.015, -0.015, d);
    float depth = clamp(1.0 - r / max(surf, 0.01), 0.0, 1.0);
    vec3 cA = colorA.rgb, cB = colorB.rgb;

    vec3 body = mix(cB * 0.3, cA, plasma * 0.85 + 0.15) * (0.3 + 0.95 * depth * depth);
    // Ripples running outwards inside the body, faster with activity.
    float rip = 0.5 + 0.5 * sin(r * 46.0 - t * (2.5 + 7.0 * energy));
    body += cA * pow(rip, 8.0) * 0.28 * depth * (0.35 + energy);
    // Facets: a faint hexagonal lattice deep inside, for a sense of structure.
    vec2 hp = uv * 9.0 + vec2(t * 0.15, 0.0);
    float hexv = abs(fract(hp.x + 0.5 * floor(hp.y)) - 0.5) + abs(fract(hp.y) - 0.5);
    body += cA * smoothstep(0.06, 0.0, abs(hexv - 0.45)) * 0.08 * depth * detail;
    // Nucleus.
    float nucleus = exp(-r * r * 38.0) * (1.0 + 1.6 * level + 0.9 * pulse + 0.4 * energy);
    body += mix(cA, vec3(1.0), 0.65) * nucleus;

    vec3 col = body * inside;
    // Rim and halo.
    float rim = exp(-abs(d) * 85.0) * (0.85 + 1.6 * level + 0.6 * energy + 0.6 * pulse);
    col += mix(cA, vec3(1.0), 0.35) * rim;
    float halo = exp(-max(d, 0.0) * 6.5) * (0.22 + 0.35 * energy + 0.55 * level) * (1.0 - inside);
    col += cA * halo * 0.55;

    // Warning / error: the light turns to the alert colour.
    float lum = dot(col, vec3(0.3, 0.5, 0.2));
    col = mix(col, alertColor.rgb * lum * 1.7, alertMix * 0.8);
    // Error: colour channels drift apart.
    col.r += instability * 0.25 * exp(-abs(r - surf - 0.02) * 60.0);
    col.b += instability * 0.25 * exp(-abs(r - surf + 0.02) * 60.0);

    float edge = smoothstep(1.0, 0.82, r);
    col = max(col * edge, 0.0);
    float alpha = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    fragColor = vec4(min(col, vec3(alpha)), alpha) * qt_Opacity;
}
