#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aFace;
uniform mat4 uMatrix;
out vec3 vPosition;
out vec3 vNormal;
out vec2 vUV;
flat out int vFace;
void main() {
    vPosition = aPosition; vNormal = aNormal; vUV = aUV; vFace = int(aFace);
    gl_Position = uMatrix * vec4(aPosition, 1);
}
