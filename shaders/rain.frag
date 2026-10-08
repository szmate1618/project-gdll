#version 330 core
in vec2 vUV;
in vec3 vWorldPosition;
in float vFade;
uniform vec3 uEye;
uniform bool uNight;
uniform vec3 uFlashlightDirection;
uniform vec3 uFlashlightColor;
uniform vec3 uFlashlightCone;
out vec4 fragColor;

void main() {
    float across = max(1.0 - abs(vUV.x * 2.0 - 1.0), 0.0);
    float along = smoothstep(0.0, 0.15, vUV.y) * (1.0 - smoothstep(0.75, 1.0, vUV.y));
    float alpha = 0.45 * across * along * vFade;
    if (alpha < 0.002) discard;
    vec3 color = uNight ? vec3(0.003, 0.005, 0.008) : vec3(0.24, 0.30, 0.38);
    if (uNight) {
        vec3 fromEye = vWorldPosition - uEye;
        float distanceFromEye = length(fromEye);
        float cone = smoothstep(uFlashlightCone.z, uFlashlightCone.y,
                               dot(fromEye / max(distanceFromEye, 0.001), uFlashlightDirection));
        float rangeFade = 1.0 - smoothstep(uFlashlightCone.x * 0.75, uFlashlightCone.x, distanceFromEye);
        color += uFlashlightColor * (0.12 * cone * rangeFade / (1.0 + 0.025 * distanceFromEye * distanceFromEye));
    }
    vec3 srgb = mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
                    step(vec3(0.0031308), color));
    fragColor = vec4(srgb, alpha);
}
