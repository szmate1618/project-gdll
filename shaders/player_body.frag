#version 330 core
in vec3 vColor;
uniform float uOpacity;
out vec4 fragColor;
void main() { fragColor = vec4(vColor, uOpacity); }
