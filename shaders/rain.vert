#version 330 core
layout(location = 0) in vec4 aSeed; // X/Z/Y phase in [0,1), falling speed multiplier.
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uEye;
uniform float uTime;
out vec2 vUV;
out vec3 vWorldPosition;
out float vFade;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
                                   vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
    // World-aligned periodic rain, wrapped only when it leaves the eye volume.
    // Moving the camera does not drag nearby drops along with it.
    const vec3 extent = vec3(18.0, 8.0, 18.0);
    vec3 velocity = vec3(0.8, -10.0 * aSeed.w, 0.25);
    vec3 phase = vec3(aSeed.x, aSeed.z, aSeed.y) * (2.0 * extent) + velocity * uTime;
    vec3 offset = mod(phase - uEye + extent, 2.0 * extent) - extent;
    vec3 center = uEye + offset;
    vec4 viewCenter = uView * vec4(center, 1.0);
    vec3 viewVelocity = mat3(uView) * velocity;
    float projectedSpeed = length(viewVelocity.xy);
    vec2 axis = projectedSpeed > 0.001 ? viewVelocity.xy / projectedSpeed : vec2(0.0, -1.0);
    vec2 side = vec2(-axis.y, axis.x);
    float width = 0.025;
    // Looking along the falling direction yields small drops rather than
    // degenerate polygons; sideways views show elongated motion streaks.
    float streakLength = mix(width, 0.55 * aSeed.w, projectedSpeed / length(velocity));
    vec2 corner = corners[gl_VertexID];
    viewCenter.xy += axis * (corner.y * streakLength) + side * (corner.x * width);
    gl_Position = uProjection * viewCenter;
    vUV = corner + 0.5;
    vWorldPosition = center;
    float edge = max(abs(offset.x) / extent.x, abs(offset.z) / extent.z);
    float vertical = abs(offset.y) / extent.y;
    vFade = (1.0 - smoothstep(0.7, 1.0, edge)) * (1.0 - smoothstep(0.8, 1.0, vertical))
            * smoothstep(0.8, 1.8, length(offset));
}
