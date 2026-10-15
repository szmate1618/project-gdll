#version 330 core
in float vShade;
out vec4 fragColor;
void main() { fragColor = vec4(vec3(vShade), 1.0); }
