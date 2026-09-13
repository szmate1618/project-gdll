#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in mat4 aInstanceModel;
layout(location = 8) in float aAnimationPhase;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform bool uInstanced;
uniform bool uUnlit;
uniform bool uAnimated;
uniform samplerBuffer uAnimation;
uniform float uAnimationFrame;
uniform int uAnimationCount;
uniform int uAnimationVertices;

out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;

void main() {
    vec3 position = aPosition;
    vec3 normal = aNormal;
    if (uAnimated) {
        float frame = mod(uAnimationFrame + aAnimationPhase * float(uAnimationCount), float(uAnimationCount));
        int first = int(floor(frame));
        int second = (first + 1) % uAnimationCount;
        int a = 2 * (first * uAnimationVertices + gl_VertexID);
        int b = 2 * (second * uAnimationVertices + gl_VertexID);
        position = mix(texelFetch(uAnimation, a).xyz, texelFetch(uAnimation, b).xyz, fract(frame));
        normal = normalize(mix(texelFetch(uAnimation, a + 1).xyz,
                               texelFetch(uAnimation, b + 1).xyz, fract(frame)));
    }
    mat4 model = uInstanced ? aInstanceModel * uModel : uModel;
    gl_Position = uProjection * uView * model * vec4(position, 1.0);
    // Impostors are unlit. Keep the general instanced path correct for shear
    // and nonuniform scale without paying for inverses on these tree vertices.
    vNormal = uInstanced ? (uUnlit ? normal : transpose(inverse(mat3(model))) * normal)
                        : uNormalMatrix * normal;
    vUV = aUV;
    vColor = aColor;
}
