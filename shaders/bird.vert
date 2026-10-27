#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aVelocity;
layout(location = 2) in float aPhase;
uniform mat4 uProjectionView;
uniform float uTime;
out vec2 vCard;
flat out int vPlane;
flat out float vFlap;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-.5, -.5), vec2(.5, -.5), vec2(.5, .5),
                                   vec2(-.5, -.5), vec2(.5, .5), vec2(-.5, .5));
    vec3 forward = dot(aVelocity, aVelocity) > .0001 ? normalize(aVelocity) : vec3(0, 0, -1);
    vec3 reference = abs(forward.y) < .95 ? vec3(0, 1, 0) : vec3(0, 0, 1);
    vec3 right = normalize(cross(forward, reference));
    vec3 up = cross(right, forward);
    vFlap = sin(uTime * 15.0 + aPhase);
    // A slight bank keeps the cheap rigid silhouettes alive. All three cards
    // remain perpendicular, and velocity determines their heading and pitch.
    float bank = .12 * vFlap;
    vec3 bankedRight = right * cos(bank) + up * sin(bank);
    up = up * cos(bank) - right * sin(bank);
    right = bankedRight;
    vCard = corners[gl_VertexID % 6];
    vPlane = gl_VertexID / 6;
    vec3 offset;
    if (vPlane == 0) offset = right * vCard.x + forward * vCard.y;
    else if (vPlane == 1) offset = forward * vCard.x + up * vCard.y;
    else offset = right * vCard.x + up * vCard.y;
    gl_Position = uProjectionView * vec4(aPosition + offset, 1);
}
