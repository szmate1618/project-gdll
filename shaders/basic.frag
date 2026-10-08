#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
in vec3 vWorldPosition;

uniform vec4 uBaseColor;
uniform sampler2D uBaseTexture;
uniform bool uHasTexture;
uniform bool uUnlit;
uniform int uAlphaMode; // 0 = opaque, 1 = mask, 2 = blend
uniform float uAlphaCutoff;
uniform bool uShadowPass;
uniform bool uShadows;
uniform sampler2DShadow uShadowMap;
uniform mat4 uSunMatrix;
uniform vec3 uEye;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform float uShadowDistance;
uniform float uShadowFadeStart;
uniform vec3 uHazeColor;
uniform float uHazeDensity;
uniform float uHazeStart;
uniform vec4 uGroundFog; // Base Y, inverse layer height, base density, clear distance.

out vec4 fragColor;

float groundFogDepth(float distanceFromEye) {
    float fogLength = max(distanceFromEye - uGroundFog.w, 0.0);
    if (uGroundFog.z <= 0.0 || fogLength <= 0.0) return 0.0;
    // Integrate only the ray segment beyond the clear foreground. Work in
    // normalized layer heights: density is clamp(1 - height, 0, 1).
    float startY = mix(uEye.y, vWorldPosition.y, uGroundFog.w / distanceFromEye);
    float a = (startY - uGroundFog.x) * uGroundFog.y;
    float b = (vWorldPosition.y - uGroundFog.x) * uGroundFog.y;
    float low = min(a, b), high = max(a, b);
    float span = high - low;
    float average;
    if (span < 0.0001) {
        // Horizontal and nearly horizontal rays have a finite limit; avoid
        // dividing two tiny differences and creating a band at the horizon.
        average = clamp(1.0 - (a + b) * 0.5, 0.0, 1.0);
    } else {
        float belowFraction = clamp(-low / span, 0.0, 1.0);
        float rampLow = clamp(low, 0.0, 1.0);
        float rampHigh = clamp(high, 0.0, 1.0);
        float rampFraction = (rampHigh - rampLow) / span;
        // The ramp is linear, so its endpoint average is the exact integral.
        average = belowFraction + rampFraction * (1.0 - (rampLow + rampHigh) * 0.5);
    }
    return uGroundFog.z * fogLength * average;
}

float sunlightVisibility(float sunFacing, float distanceFromEye) {
    if (!uShadows || sunFacing <= 0.0) return 1.0;
    if (distanceFromEye >= uShadowDistance) return 1.0;
    vec4 lightClip = uSunMatrix * vec4(vWorldPosition, 1.0);
    vec3 coord = lightClip.xyz / lightClip.w * 0.5 + 0.5;
    if (any(lessThan(coord, vec3(0.0))) || any(greaterThan(coord, vec3(1.0)))) return 1.0;
    // Slope-aware receiver bias complements polygon offset on the caster pass.
    float bias = max(0.00006, 0.0003 * (1.0 - sunFacing));
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visibility = 0.0;
    // Four hardware-filtered depth comparisons keep edges soft at low cost.
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            visibility += texture(uShadowMap, vec3(coord.xy + (vec2(x, y) - 0.5) * texel,
                                                    coord.z - bias));
    float fade = smoothstep(uShadowFadeStart, uShadowDistance, distanceFromEye);
    return mix(visibility * 0.25, 1.0, fade);
}

void main() {
    vec4 color = uBaseColor * vColor;
    if (uHasTexture && (!uShadowPass || uAlphaMode == 1)) color *= texture(uBaseTexture, vUV);
    if (uAlphaMode == 1 && color.a < uAlphaCutoff) discard;
    if (uShadowPass) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;
    float sunFacing = max(dot(normal, uSunDirection), 0.0);
    vec3 ambient = mix(uGroundAmbient, uSkyAmbient, normal.y * 0.5 + 0.5);
    float distanceFromEye = length(vWorldPosition - uEye);
    vec3 light = uUnlit ? vec3(1.0) : ambient + uSunColor * sunFacing * sunlightVisibility(sunFacing, distanceFromEye);
    vec3 linearColor = max(color.rgb * light, vec3(0.0));
    // Haze affects unlit tree cards too, after alpha discard and lighting but
    // before output encoding. Keep material alpha intact for blended surfaces.
    if (uHazeDensity > 0.0 || uGroundFog.z > 0.0) {
        float opticalDepth = max(uHazeDensity, 0.0) * max(distanceFromEye - uHazeStart, 0.0)
                           + groundFogDepth(distanceFromEye);
        float transmission = exp(-opticalDepth);
        linearColor = mix(uHazeColor, linearColor, transmission);
    }
    // Base-color textures are uploaded as sRGB and decoded by the sampler.
    // Encode explicitly so offscreen and native framebuffers look the same.
    vec3 srgb = mix(12.92 * linearColor,
                    1.055 * pow(linearColor, vec3(1.0 / 2.4)) - 0.055,
                    step(vec3(0.0031308), linearColor));
    fragColor = vec4(srgb, uAlphaMode == 2 ? color.a : 1.0);
}
