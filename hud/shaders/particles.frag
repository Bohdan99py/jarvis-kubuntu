#version 440
// Particles around the core, computed here instead of simulated on the CPU:
//  * dust orbiting in two bands, each grain living a few seconds;
//  * an inflow drawn into the core while Jarvis listens or works;
//  * a burst thrown out of the core on an event.
// A grain's place follows from the clock and a hash of its cell, so each pixel
// looks only at the grains of its own and the neighbouring angular cells.
// Units: the core's square has a half-size of 1; this item is `scale` times larger.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float pixel;        // one pixel in square units
    float scale;        // item size / square size
    float orbit;        // turns of the orbiting frame
    float inflow;       // phase of the inflow (advanced with the state's pace)
    float inflowRate;   // 0..1 share of inflow grains alive
    float density;      // 1 balanced, more in cinematic
    float burstAge;     // seconds since the last burst, < 0: none
    float burstSeed;
    float burstCount;
    vec4 color;
};

const float TAU = 6.2831853;

float hash(float n) { return fract(sin(n * 127.1 + 0.37) * 43758.5453); }

// A soft grain at polar (pr, pa) seen from (r, a): distance in the polar
// approximation (good at grain scale), size in pixels.
float grain(float r, float a, float pr, float pa, float sizePx) {
    float da = abs(fract((a - pa) / TAU + 0.5) - 0.5) * TAU;
    float dr = r - pr;
    float d2 = dr * dr + (r * da) * (r * da);
    float s = sizePx * pixel;
    return exp(-d2 / (s * s));
}

float orbitBand(float r, float a, float radius, float cells, float seed) {
    if (abs(r - radius) > 0.08) return 0.0;
    float turnA = fract(a / TAU - orbit);
    float id0 = floor(turnA * cells);
    float v = 0.0;
    for (int k = -1; k <= 1; ++k) {
        float id = mod(id0 + float(k), cells);
        float h = hash(id * 1.37 + seed);
        float h2 = hash(id * 2.11 + seed + 5.0);
        float pa = ((id + h) / cells + orbit) * TAU;
        float pr = radius + (h2 - 0.5) * 0.07;
        float life = 3.2 + 4.0 * h;
        float age = fract(time / life + h2);
        float fade = smoothstep(0.0, 0.12, age) * smoothstep(1.0, 0.55, age);
        v += grain(r, a, pr, pa, 1.1 + 2.0 * h2 * (1.0 - 0.6 * age)) * fade * (0.35 + 0.45 * h);
    }
    return v;
}

float inflowLayer(float r, float a) {
    if (inflowRate < 0.01 || r < 0.28 || r > 1.2) return 0.0;
    float cells = floor(40.0 * density);
    float id0 = floor(fract(a / TAU) * cells);
    float v = 0.0;
    for (int k = -1; k <= 1; ++k) {
        float id = mod(id0 + float(k), cells);
        for (int j = 0; j < 2; ++j) {
            float hj = hash(id * 5.3 + float(j) * 17.0 + 3.0);
            if (hj > inflowRate) continue;
            float p = fract(inflow * (0.7 + 0.6 * hj) + hash(id + float(j) * 7.0));
            float pr = mix(1.15, 0.3, p * p);                 // speeds up towards the core
            float pa = (id + 0.2 + 0.6 * hj) / cells * TAU;
            float fade = smoothstep(0.0, 0.2, p) * smoothstep(1.0, 0.8, p);
            v += grain(r, a, pr, pa, 1.6 + 1.8 * hj) * fade * 0.8;
        }
    }
    return v;
}

float burstLayer(float r, float a) {
    if (burstAge < 0.0 || burstAge > 1.8 || r > 1.25) return 0.0;
    float cells = burstCount;
    float id0 = floor(fract(a / TAU) * cells);
    float v = 0.0;
    for (int k = -1; k <= 1; ++k) {
        float id = mod(id0 + float(k), cells);
        float h = hash(id * 3.7 + burstSeed);
        float life = 1.0 + 0.6 * h;
        if (burstAge > life) continue;
        float speed = 0.5 + 0.5 * hash(id * 9.1 + burstSeed);
        float pr = 0.12 + speed * (1.0 - exp(-burstAge * 2.2)) * 0.9;
        float pa = (id + h) / cells * TAU;
        v += grain(r, a, pr, pa, 2.6 * (1.0 - burstAge / life) + 0.6) * (1.0 - burstAge / life);
    }
    return v;
}

void main() {
    vec2 uv = (qt_TexCoord0 * 2.0 - 1.0) * scale;
    float r = length(uv);
    bool nearOrbit = abs(r - 0.86) < 0.08 || abs(r - 0.58) < 0.08;
    bool bursting = burstAge >= 0.0 && burstAge <= 1.8;
    if (!nearOrbit && inflowRate < 0.01 && !bursting)
        discard;
    float a = atan(uv.y, uv.x);
    float cellsOuter = floor(70.0 * density), cellsInner = floor(44.0 * density);
    float dust = orbitBand(r, a, 0.86, cellsOuter, 1.0) + orbitBand(r, a, 0.58, cellsInner, 7.0);
    float flow = inflowLayer(r, a);
    float burst = burstLayer(r, a);
    vec3 col = mix(color.rgb, vec3(1.0), 0.3) * dust * 0.55 + color.rgb * flow * 0.7
             + mix(color.rgb, vec3(1.0), 0.4) * burst * 0.9;
    float alpha = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    if (alpha < 0.002)
        discard;
    fragColor = vec4(min(col, vec3(alpha)), alpha) * qt_Opacity;
}
