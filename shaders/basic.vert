#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in mat4 aInstanceModel;
// Zombie VAOs: phase, clip first frame, clip frame count (xyz).
// Tree VAOs: foot Y, inverse visible height, lower light, upper light.
layout(location = 8) in vec4 aInstanceInfo;
layout(location = 9) in float aAnimationFrame;
layout(location = 10) in vec2 aSkinningAndStep; // Live skinning, stepped animation.
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
uniform bool uTreeCanopyShading;
uniform int uTreeBillboardMode; // 0 authored, 1 root-yaw side card, 2 eye-facing top card.
uniform vec3 uTreeBillboardPivot;
uniform vec3 uEye;
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
out float vCanopyHeight;
flat out vec2 vCanopyLight;

vec3 turnToward(vec3 point, vec3 from, vec3 toward) {
    float cosine = clamp(dot(from, toward), -1.0, 1.0);
    if (cosine < -0.9999) {
        vec3 axis = normalize(cross(from, abs(from.x) < 0.9 ? vec3(1,0,0) : vec3(0,0,1)));
        return 2.0 * axis * dot(axis, point) - point;
    }
    vec3 axis = cross(from, toward);
    return point + cross(axis, point) + cross(axis, cross(axis, point)) / (1.0 + cosine);
}

void main() {
    vec3 position = aPosition;
    vec3 normal = aNormal;
    if (uAnimated && aSkinningAndStep.x > 0.5 && uSkinningEnabled && uSkinningBoneCount > 0) {
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
        int count = aInstanceInfo.z > 0.0 ? int(aInstanceInfo.z) : uAnimationCount;
        int offset = int(aInstanceInfo.y);
        float frame = aAnimationFrame >= 0.0 ? aAnimationFrame :
            mod(uAnimationFrame + aInstanceInfo.x * float(count), float(count));
        int localFirst = int(floor(frame));
        int first = offset + localFirst;
        int second = offset + (localFirst + 1) % count;
        int a = 2 * (first * uAnimationVertices + gl_VertexID);
        int b = 2 * (second * uAnimationVertices + gl_VertexID);
        position = texelFetch(uAnimation, a).xyz;
        if (!uShadowPass) normal = texelFetch(uAnimation, a + 1).xyz;
        if (aSkinningAndStep.y < 0.5) {
            position = mix(position, texelFetch(uAnimation, b).xyz, fract(frame));
            if (!uShadowPass) normal = mix(normal, texelFetch(uAnimation, b + 1).xyz, fract(frame));
        }
        if (!uShadowPass) normal = normalize(normal);
    }
    mat4 model = uInstanced ? aInstanceModel * uModel : uModel;
    vec4 worldPosition = model * vec4(position, 1.0);
    if (uInstanced && uTreeBillboardMode == 1) {
        vec3 root = aInstanceModel[3].xyz;
        vec3 front = cross(model[0].xyz, model[1].xyz);
        vec2 from = front.xz;
        vec2 toward = uEye.xz - root.xz;
        if (dot(from,from) > 1e-12 && dot(toward,toward) > 1e-12) {
            from = normalize(from); toward = normalize(toward);
            float cosine = dot(from, toward);
            float sine = from.y * toward.x - from.x * toward.y;
            vec2 p = worldPosition.xz - root.xz;
            worldPosition.xz = root.xz + vec2(cosine*p.x+sine*p.y, -sine*p.x+cosine*p.y);
        }
    } else if (uInstanced && uTreeBillboardMode == 2) {
        vec3 pivot = (model * vec4(uTreeBillboardPivot, 1.0)).xyz;
        vec3 from = cross(model[2].xyz, model[0].xyz);
        vec3 toward = uEye - pivot;
        if (dot(from,from) > 1e-12 && dot(toward,toward) > 1e-12)
            worldPosition.xyz = pivot + turnToward(worldPosition.xyz-pivot, normalize(from), normalize(toward));
    }
    gl_Position = uProjection * uView * worldPosition;
    vWorldPosition = worldPosition.xyz;
    vCanopyHeight = uTreeCanopyShading ? (worldPosition.y - aInstanceInfo.x) * aInstanceInfo.y : 0.0;
    vCanopyLight = uTreeCanopyShading ? aInstanceInfo.zw : vec2(1.0);
    // Impostors are unlit. Keep the general instanced path correct for shear
    // and nonuniform scale without paying for inverses on these tree vertices.
    vNormal = uShadowPass ? vec3(0.0) : uInstanced ? (uUnlit ? normal : transpose(inverse(mat3(model))) * normal)
                        : uNormalMatrix * normal;
    vUV = aUV;
    vColor = aColor;
}
