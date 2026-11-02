#version 330 core
in vec2 vPosition;
uniform samplerCube uColor;
uniform samplerCube uDepth;
uniform mat3 uInverseView;
uniform mat4 uProjection;
uniform vec2 uInverseProjection;
out vec4 fragColor;
void main() {
    vec3 ray = uInverseView * vec3(vPosition * uInverseProjection, -1);
    float depth = texture(uDepth, ray).r;
    fragColor = texture(uColor, ray);
    gl_FragDepth = 1.0;
    if (depth < 1.0) {
        // Cube depth records distance along its face normal. Convert it to
        // current-camera depth; nearby actors must occlude this background.
        float faceDistance = 100.0 * 20000.0 / (20000.0 - depth * (20000.0 - 100.0));
        float cameraDistance = faceDistance / max(max(abs(ray.x), abs(ray.y)), abs(ray.z));
        vec4 clip = uProjection * vec4(0, 0, -cameraDistance, 1);
        float currentDepth = clip.z / clip.w * 0.5 + 0.5;
        if (currentDepth > 1.0) discard;
        gl_FragDepth = currentDepth;
    }
}
