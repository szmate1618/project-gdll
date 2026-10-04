#version 330 core

in vec2 vNdc;
out vec4 fragColor;

uniform int uMode;
uniform float uAspect;

float band(float value, float halfWidth) {
    float edge = fwidth(value) + 0.001;
    return 1.0 - smoothstep(halfWidth, halfWidth + edge, abs(value));
}

float crosshair(vec2 point, float armLength, float halfWidth, float centerGap) {
    float outsideGap = centerGap == 0.0 ? 1.0 : smoothstep(centerGap, centerGap + 0.003, abs(point.x));
    float horizontal = band(point.y, halfWidth) *
        (1.0 - smoothstep(armLength, armLength + 0.003, abs(point.x))) *
        outsideGap;
    outsideGap = centerGap == 0.0 ? 1.0 : smoothstep(centerGap, centerGap + 0.003, abs(point.y));
    float vertical = band(point.x, halfWidth) *
        (1.0 - smoothstep(armLength, armLength + 0.003, abs(point.y))) *
        outsideGap;
    return max(horizontal, vertical);
}

void main() {
    // Scaling X produces circular geometry on non-square framebuffers.
    vec2 point = vec2(vNdc.x * uAspect, vNdc.y);
    if (uMode == 0) {
        float mark = crosshair(point, 0.030, 0.0018, 0.0);
        fragColor = vec4(vec3(0.95), mark * 0.90);
        return;
    }

    float radius = length(point);
    // NDC vertical edges are at +/-1; leave only a narrow rim at top and bottom.
    const float scopeRadius = 0.97;
    float tunnel = smoothstep(scopeRadius - 0.02, scopeRadius + 0.02, radius);
    float ring = band(radius - scopeRadius, 0.004);
    float marks = crosshair(point, uMode == 1 ? 0.13 : 0.19, 0.0015, 0.018);
    // Longer zoom uses the familiar finer center point without filling it in.
    float center = uMode == 2 ? band(radius - 0.008, 0.0012) : 0.0;
    float sight = max(max(ring, marks), center);
    vec3 color = mix(vec3(0.0), vec3(0.82, 0.90, 0.82), sight);
    float alpha = max(tunnel * 0.94, sight * 0.92);
    fragColor = vec4(color, alpha);
}
