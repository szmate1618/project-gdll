#version 330 core
in vec3 vNormal;
uniform vec3 uViewDirection;
out vec4 fragColor;

void main() {
    vec3 light = normalize(-uViewDirection + vec3(0.0, 0.65, 0.0));
    float shade = 0.62 + 0.38 * max(dot(normalize(vNormal), light), 0.0);
    fragColor = vec4(vec3(0.05, 0.9, 0.18) * shade, 0.5);
}
