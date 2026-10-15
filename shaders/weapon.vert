#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aShade;
uniform mat4 uTransform;
out float vShade;
void main() {
    vShade = aShade;
    gl_Position = uTransform * vec4(aPosition, 1.0);
}
