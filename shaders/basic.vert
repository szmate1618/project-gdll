#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in mat4 aInstanceModel;
layout(location = 8) in float aAnimationPhase;
layout(location = 9) in float aAnimationFrame;
layout(location = 10) in float aLiveSkinning;
layout(location = 11) in float aBoneBase;
layout(location = 12) in uvec4 aJoints0;
layout(location = 13) in vec4 aWeights0;
layout(location = 14) in uvec4 aJoints1;
layout(location = 15) in vec4 aWeights1;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform bool uInstanced;
uniform bool uUnlit;
uniform bool uShadowPass;
uniform bool uAnimated;
uniform samplerBuffer uAnimation;
uniform float uAnimationFrame;
uniform int uAnimationCount;
uniform int uAnimationVertices;
uniform bool uSkinningEnabled;
uniform samplerBuffer uSkinningBones;
uniform int uSkinningBoneCount;

out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
out vec3 vWorldPosition;

void main() {
    vec3 position = aPosition;
    vec3 normal = aNormal;
    if (uAnimated && aLiveSkinning > 0.5 && uSkinningEnabled && uSkinningBoneCount > 0) {
        vec4 skinnedPosition = vec4(0.0);
        vec3 skinnedNormal = vec3(0.0);
        for (int influence = 0; influence < 8; ++influence) {
            uint joint = influence < 4 ? aJoints0[influence] : aJoints1[influence - 4];
            float weight = influence < 4 ? aWeights0[influence] : aWeights1[influence - 4];
            if (weight > 0.0 && int(joint) < uSkinningBoneCount) {
                int texel = (int(aBoneBase) + int(joint)) * 4;
                mat4 bone = mat4(texelFetch(uSkinningBones, texel),
                                 texelFetch(uSkinningBones, texel + 1),
                                 texelFetch(uSkinningBones, texel + 2),
                                 texelFetch(uSkinningBones, texel + 3));
                skinnedPosition += bone * vec4(aPosition, 1.0) * weight;
                if (!uShadowPass) skinnedNormal += transpose(inverse(mat3(bone))) * aNormal * weight;
            }
        }
        position = skinnedPosition.xyz;
        if (!uShadowPass) normal = normalize(skinnedNormal);
    } else if (uAnimated) {
        float frame = aAnimationFrame >= 0.0 ? aAnimationFrame :
            mod(uAnimationFrame + aAnimationPhase * float(uAnimationCount), float(uAnimationCount));
        int first = int(floor(frame));
        int second = (first + 1) % uAnimationCount;
        int a = 2 * (first * uAnimationVertices + gl_VertexID);
        int b = 2 * (second * uAnimationVertices + gl_VertexID);
        position = mix(texelFetch(uAnimation, a).xyz, texelFetch(uAnimation, b).xyz, fract(frame));
        if (!uShadowPass) normal = normalize(mix(texelFetch(uAnimation, a + 1).xyz,
                               texelFetch(uAnimation, b + 1).xyz, fract(frame)));
    }
    mat4 model = uInstanced ? aInstanceModel * uModel : uModel;
    vec4 worldPosition = model * vec4(position, 1.0);
    gl_Position = uProjection * uView * worldPosition;
    vWorldPosition = worldPosition.xyz;
    // Impostors are unlit. Keep the general instanced path correct for shear
    // and nonuniform scale without paying for inverses on these tree vertices.
    vNormal = uShadowPass ? vec3(0.0) : uInstanced ? (uUnlit ? normal : transpose(inverse(mat3(model))) * normal)
                        : uNormalMatrix * normal;
    vUV = aUV;
    vColor = aColor;
}
