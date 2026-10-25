#version 330 core
uniform sampler2D uTexture;
uniform bool uTextured;
uniform bool uMinimap;
uniform vec2 uMinimapOrigin;
uniform vec2 uMinimapX;
uniform vec2 uMinimapY;
in vec4 vColor;
in vec2 vUv;
out vec4 fragColor;
void main() {
    if (uMinimap) {
        vec2 uv = uMinimapOrigin + vUv.x * uMinimapX + vUv.y * uMinimapY;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            fragColor = vec4(0.07, 0.10, 0.08, 1.0);
        } else {
            // Scene textures use sRGB storage; the HUD target expects encoded
            // colours. Recover the original imagery without scene lighting.
            vec3 linear = texture(uTexture, uv).rgb;
            vec3 srgb = mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
                            greaterThan(linear, vec3(0.0031308)));
            fragColor = vec4(srgb, 1.0);
        }
        return;
    }
    fragColor = vColor * (uTextured ? texture(uTexture, vUv) : vec4(1.0));
}
