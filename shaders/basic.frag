#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

uniform vec4 uBaseColor;
uniform sampler2D uBaseTexture;
uniform bool uHasTexture;
uniform bool uUnlit;
uniform int uAlphaMode; // 0 = opaque, 1 = mask, 2 = blend
uniform float uAlphaCutoff;

out vec4 fragColor;

void main() {
    vec4 color = uBaseColor * vColor;
    if (uHasTexture) color *= texture(uBaseTexture, vUV);
    if (uAlphaMode == 1 && color.a < uAlphaCutoff) discard;
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;
    vec3 sunDirection = normalize(vec3(-0.4, 0.85, 0.3));
    float light = uUnlit ? 1.0 : 0.28 + 0.72 * max(dot(normal, sunDirection), 0.0);
    vec3 linearColor = max(color.rgb * light, vec3(0.0));
    // Base-color textures are uploaded as sRGB and decoded by the sampler.
    // Encode explicitly so offscreen and native framebuffers look the same.
    vec3 srgb = mix(12.92 * linearColor,
                    1.055 * pow(linearColor, vec3(1.0 / 2.4)) - 0.055,
                    step(vec3(0.0031308), linearColor));
    fragColor = vec4(srgb, uAlphaMode == 2 ? color.a : 1.0);
}
