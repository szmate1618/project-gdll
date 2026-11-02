#version 330 core
out vec2 vPosition;
void main() {
    vPosition = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(vPosition, 0, 1);
}
