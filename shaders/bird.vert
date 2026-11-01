#version 330 core
layout(location = 0) in vec2 aUv;
layout(location = 3) in vec3 aPosition;
layout(location = 4) in vec3 aVelocity;
layout(location = 5) in float aPhase;
uniform mat4 uProjectionView;
uniform samplerBuffer uAnimation;
uniform float uFrame;
uniform int uFrameCount;
uniform int uVertexCount;
out vec2 vUv;
out vec3 vNormal;

void main() {
    float frame = mod(uFrame + aPhase * (float(uFrameCount) / 6.28318530718), float(uFrameCount));
    int first = int(floor(frame));
    int next = (first + 1) % uFrameCount;
    int a = (first * uVertexCount + gl_VertexID) * 2;
    int b = (next * uVertexCount + gl_VertexID) * 2;
    vec3 position = mix(texelFetch(uAnimation, a).xyz, texelFetch(uAnimation, b).xyz, fract(frame));
    vec3 normal = mix(texelFetch(uAnimation, a + 1).xyz, texelFetch(uAnimation, b + 1).xyz, fract(frame));
    vec3 forward = dot(aVelocity, aVelocity) > .0001 ? normalize(aVelocity) : vec3(0, 0, -1);
    vec3 reference = abs(forward.y) < .95 ? vec3(0, 1, 0) : vec3(0, 0, 1);
    // glTF pigeon faces local +Z. This basis is a proper rotation, including
    // vertical flight; positions remain centered on the boid root.
    vec3 right = normalize(cross(reference, forward));
    vec3 up = cross(forward, right);
    mat3 rotation = mat3(right, up, forward);
    gl_Position = uProjectionView * vec4(aPosition + rotation * position, 1);
    vNormal = rotation * normal;
    vUv = aUv;
}
