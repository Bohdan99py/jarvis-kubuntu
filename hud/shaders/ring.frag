#version 440
// One ring of the core's machinery: a band cut into segments, tick marks, an
// optional partial arc, a travelling light pulse and a glitch that shifts
// segments. Every ring of the HUD is an instance with its own parameters.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float pixel;        // one pixel in uv units (2 / item size)
    float radius;       // 0..1 of the half size
    float thickness;
    float segments;
    float duty;         // lit share of every segment
    float turn;     // turns
    float ticks;        // tick marks around the ring, 0 = none
    float tickLength;
    float tickTurn; // turns
    float arcStart;     // turns, relative to turn
    float arcSpan;      // 1 = full circle
    float pulseAngle;   // turns
    float pulseStrength;
    float glitch;
    float intensity;
    float dim;          // share of dimmed segments 0..1
    float seed;
    vec4 color;
    vec4 alertColor;
    float alertMix;
};

float hash(float n) { return fract(sin(n * 91.345 + seed * 17.13) * 47453.5453); }

void main() {
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    float r = length(uv);
    float ang = fract(atan(uv.y, uv.x) / 6.2831853 + 1.0);
    float rel = fract(ang - turn + 1.0);

    // Segments: which one, where inside it.
    float id = floor(rel * segments);
    float within = fract(rel * segments);
    float h = hash(id);
    float segPx = pixel / max(r * 6.2831853 / segments, 1e-4);
    float seg = smoothstep(0.0, segPx, within) * smoothstep(duty, duty - segPx, within);
    float bright = h < dim ? 0.3 : 1.0;
    // Glitch: some segments jump in and out of their orbit.
    float jump = glitch * (hash(id + floor(time * 9.0)) - 0.5) * 0.06;
    float band = smoothstep(thickness * 0.5 + pixel, thickness * 0.5 - pixel, abs(r - radius - jump));

    // A partial arc.
    float arcRel = fract(rel - arcStart + 1.0);
    float arc = arcSpan >= 0.999 ? 1.0 : smoothstep(0.0, 0.004, arcRel) * smoothstep(arcSpan, arcSpan - 0.004, arcRel);

    // Tick marks outside the band.
    float tick = 0.0;
    if (ticks > 0.5) {
        float tr = fract(ang - tickTurn + 1.0) * ticks;
        float tw = abs(fract(tr) - 0.5) * (r * 6.2831853 / ticks);
        float major = step(0.5, fract(floor(tr) / 5.0) < 0.2 ? 1.0 : 0.0);
        float len = tickLength * (1.0 + major);
        float inBand = step(radius + thickness * 0.5 + pixel, r) * step(r, radius + thickness * 0.5 + pixel + len);
        tick = inBand * smoothstep(pixel * 1.2, 0.0, tw) * (0.45 + 0.4 * major);
    }

    // The travelling pulse.
    float dp = abs(fract(ang - pulseAngle + 0.5) - 0.5);
    float pulse = exp(-dp * 70.0) * pulseStrength * smoothstep(thickness * 3.0, 0.0, abs(r - radius));

    float v = (band * seg * bright * arc + tick * arc) * intensity + pulse;
    vec3 col = mix(color.rgb, alertColor.rgb, alertMix * (0.4 + 0.6 * step(0.6, h))) * v;
    col += vec3(1.0) * pulse * 0.35;
    float alpha = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    fragColor = vec4(min(col, vec3(alpha)), alpha) * qt_Opacity;
}
