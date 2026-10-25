#version 330 core
uniform sampler2D uTexture;
uniform bool uTextured;
in vec4 vColor;
in vec2 vUv;
out vec4 fragColor;
void main() {
    fragColor = vColor * (uTextured ? texture(uTexture, vUv) : vec4(1.0));
}
