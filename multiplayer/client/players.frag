#version 330 core
uniform vec3 uColor;
uniform sampler2DArray uAppearance;
uniform bool uTextured;
in vec2 vUV;
flat in int vFace;
out vec4 fragColor;
void main() { fragColor = vec4(uTextured ? texture(uAppearance, vec3(vUV, vFace)).rgb : uColor, 1.0); }
