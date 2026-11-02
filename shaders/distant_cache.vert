#version 330 core
uniform mat4 uTransform;
out vec2 vUV;
void main() {
    vUV = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec4 clip = uTransform * vec4(vUV * 2.0 - 1.0, -1, 1);
    // Background only: full-rate geometry always draws over this rectangle.
    gl_Position = vec4(clip.xy, clip.w, clip.w);
}
