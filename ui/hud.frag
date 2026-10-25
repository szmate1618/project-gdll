#version 330 core
uniform sampler2D uTexture;
uniform bool uTextured;
uniform bool uMinimap;
uniform bool uMinimapRoads;
uniform bool uMinimapRoadOverlay;
uniform float uMapContrast;
uniform vec2 uMinimapOrigin;
uniform vec2 uMinimapX;
uniform vec2 uMinimapY;
in vec4 vColor;
in vec2 vUv;
out vec4 fragColor;
void main() {
    if (uMinimapRoadOverlay) {
        fragColor = texture(uTexture, vec2(vUv.x, 1.0 - vUv.y));
        return;
    }
    if (uMinimapRoads) {
        if (any(lessThan(vUv, vec2(0.0))) || any(greaterThan(vUv, vec2(1.0)))) discard;
        fragColor = vec4(0.90, 0.86, 0.72, 1.0);
        return;
    }
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
            // The minimap requests a contrast boost; the full map requests
            // original colors. Both adjustments operate in encoded RGB.
            srgb = clamp((srgb - 0.5) * uMapContrast + 0.5, 0.0, 1.0);
            fragColor = vec4(srgb, 1.0);
        }
        return;
    }
    fragColor = vColor * (uTextured ? texture(uTexture, vUv) : vec4(1.0));
}
