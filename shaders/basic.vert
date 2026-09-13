#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
layout(location = 4) in mat4 aInstanceModel;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform bool uInstanced;
uniform bool uUnlit;

out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;

void main() {
    mat4 model = uInstanced ? aInstanceModel * uModel : uModel;
    gl_Position = uProjection * uView * model * vec4(aPosition, 1.0);
    // Impostors are unlit. Keep the general instanced path correct for shear
    // and nonuniform scale without paying for inverses on these tree vertices.
    vNormal = uInstanced ? (uUnlit ? aNormal : transpose(inverse(mat3(model))) * aNormal)
                        : uNormalMatrix * aNormal;
    vUV = aUV;
    vColor = aColor;
}
