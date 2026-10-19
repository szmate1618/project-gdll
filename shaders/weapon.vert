#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aPump;
uniform mat4 uTransform;
uniform mat4 uPose;
uniform float uPumpTravel;
out vec3 vColor;
out vec3 vEyePosition;
out vec3 vEyeNormal;
out vec3 vBaseColor;
void main() {
    float light = 0.40 + 0.60 * max(dot(normalize(aNormal), normalize(vec3(-0.5, 0.8, 0.6))), 0.0);
    vColor = aColor * light;
    vBaseColor = aColor;
    vec3 position = aPosition + vec3(0.0, 0.0, aPump * uPumpTravel);
    vEyePosition = vec3(uPose * vec4(position, 1.0));
    vEyeNormal = mat3(uPose) * aNormal; // Weapon poses are rigid.
    gl_Position = uTransform * vec4(position, 1.0);
}
