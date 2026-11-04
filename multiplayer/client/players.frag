#version 330 core
uniform vec3 uColor;
uniform sampler2DArray uAppearance;
uniform bool uTextured;
uniform vec3 uDayNightTint;
in vec2 vUV;
flat in int vFace;
out vec4 fragColor;
void main() {
    vec3 color = uTextured ? texture(uAppearance, vec3(vUV, vFace)).rgb : uColor;
    fragColor = vec4(color * uDayNightTint, 1.0);
}
