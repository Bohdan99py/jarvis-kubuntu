#version 440
// Data streams: beams that converge on the core (a request on its way to the
// model) or leave it (the answer arriving), with packets whose speed follows
// the real rate of the answer.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float pixel;
    float aspect;       // width / height
    float beams;
    float turn;     // turns
    float travel;       // packet phase, advanced by the HUD (speed follows the stream)
    float direction;    // +1 outwards, -1 inwards
    float innerRadius;
    float intensity;
    vec4 color;
};

float hash(float n) { return fract(sin(n * 78.233) * 43758.5453); }

void main() {
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    uv.x *= aspect;
    float r = length(uv);
    float ang = fract(atan(uv.y, uv.x) / 6.2831853 + 1.0 - turn);
    float id = floor(ang * beams);
    float h = hash(id);
    // Beams are not evenly spread: each sits somewhere in its sector.
    float centre = (id + 0.25 + 0.5 * h) / beams;
    float dAng = abs(fract(ang - centre + 0.5) - 0.5) * 6.2831853 * r;
    float beam = exp(-dAng / (pixel * (1.2 + 2.0 * h)));
    float packets = fract(r * (3.0 + 3.0 * h) - direction * travel * (0.6 + 0.8 * h) + h * 7.0);
    float packet = smoothstep(0.82, 0.97, packets) * smoothstep(1.0, 0.97, packets);
    float reach = smoothstep(innerRadius, innerRadius + 0.08, r) * smoothstep(1.6, 0.6, r);
    float v = (beam * 0.18 + beam * packet * 1.6) * reach * intensity * (0.4 + 0.6 * step(0.35, h));
    vec3 col = color.rgb * v;
    float alpha = clamp(max(max(col.r, col.g), col.b), 0.0, 1.0);
    fragColor = vec4(min(col, vec3(alpha)), alpha) * qt_Opacity;
}
