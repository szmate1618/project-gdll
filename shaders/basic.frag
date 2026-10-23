#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
in vec3 vWorldPosition;
in float vCanopyHeight;
uniform vec3 uForestVisibility; // World X/Z center, horizontal radius; zero disables.
flat in vec2 vCanopyLight;
flat in int vWoundBase;
uniform usamplerBuffer uZombieWounds;
uniform int uZombieTriangleOffset;
uniform bool uZombieCullBackfaces;

uniform vec4 uBaseColor;
uniform sampler2D uBaseTexture;
uniform bool uHasTexture;
uniform bool uUnlit;
uniform int uAlphaMode; // 0 = opaque, 1 = mask, 2 = blend
uniform float uAlphaCutoff;
uniform float uMinifiedAlphaCutoff; // Zero keeps the material's fixed cutoff.
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
uniform float uNightBlend;
uniform vec3 uUnlitLight;
uniform vec3 uFlashlightPosition;
uniform vec3 uFlashlightDirection;
uniform bool uFlashlightShadows;
uniform sampler2DShadow uFlashlightShadowMap;
uniform mat4 uFlashlightMatrix;
uniform vec3 uFlashlightShadowDistances; // Near plane, fade start, shadow distance, in meters.
uniform vec3 uFlashlightColor;
uniform vec3 uFlashlightCone; // Range in meters, cosine of inner/outer half angles.
uniform vec3 uMuzzleFlashPosition;
uniform vec3 uMuzzleFlashColor;
uniform float uMuzzleFlashRange;
uniform int uExplosionCount;
uniform vec3 uExplosionPositions[8];
uniform vec3 uExplosionColors[8];
uniform float uExplosionRanges[8];

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

float flashlightVisibility(float facing, float distanceFromLight) {
    if (!uFlashlightShadows || distanceFromLight >= uFlashlightShadowDistances.z) return 1.0;
    vec4 clip = uFlashlightMatrix * vec4(vWorldPosition, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
    if (any(lessThan(coord, vec3(0.0))) || any(greaterThan(coord, vec3(1.0)))) return 1.0;
    // Perspective depth is nonlinear. Convert a small world-space receiver bias
    // to depth units; a fixed normalized bias would erase distant spot shadows.
    float meters = max(0.002, 0.01 * (1.0 - facing));
    float near = uFlashlightShadowDistances.x;
    float far = uFlashlightShadowDistances.z;
    float bias = (far * near / (far - near)) * meters / (clip.w * max(clip.w - meters, near));
    vec2 texel = 1.0 / vec2(textureSize(uFlashlightShadowMap, 0));
    float visibility = 0.0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            visibility += texture(uFlashlightShadowMap,
                vec3(coord.xy + (vec2(x, y) - 0.5) * texel, coord.z - bias));
    float fade = smoothstep(uFlashlightShadowDistances.y, far, distanceFromLight);
    return mix(visibility * 0.25, 1.0, fade);
}

vec3 flashlight(vec3 normal) {
    if (uNightBlend <= 0.0) return vec3(0.0);
    float distanceFromLight = length(vWorldPosition - uFlashlightPosition);
    if (distanceFromLight >= uFlashlightCone.x) return vec3(0.0);
    vec3 fromLight = (vWorldPosition - uFlashlightPosition) / max(distanceFromLight, 0.001);
    float cone = smoothstep(uFlashlightCone.z, uFlashlightCone.y,
                           dot(fromLight, uFlashlightDirection));
    float rangeFade = 1.0 - smoothstep(uFlashlightCone.x * 0.75, uFlashlightCone.x, distanceFromLight);
    float falloff = 1.0 / (1.0 + 0.025 * distanceFromLight * distanceFromLight);
    // Impostor normals describe a card rather than the photographed canopy.
    float facing = uUnlit ? 1.0 : max(dot(normal, -fromLight), 0.0);
    if (cone <= 0.0 || facing <= 0.0) return vec3(0.0);
    float visibility = flashlightVisibility(facing, distanceFromLight);
    return uFlashlightColor * (uNightBlend * cone * rangeFade * falloff * facing * visibility);
}

vec3 pointLight(vec3 normal, vec3 position, vec3 color, float range) {
    if (all(equal(color, vec3(0.0)))) return vec3(0.0);
    vec3 toLight = position - vWorldPosition;
    float distanceFromLight = length(toLight);
    if (distanceFromLight >= range) return vec3(0.0);
    float rangeFade = 1.0 - smoothstep(range * 0.75, range, distanceFromLight);
    float facing = uUnlit ? 1.0 : max(dot(normal, toLight / max(distanceFromLight, 0.001)), 0.0);
    return color * (rangeFade * facing / (1.0 + distanceFromLight * distanceFromLight));
}

float alphaMaskCutoff() {
    if (!uHasTexture || uMinifiedAlphaCutoff <= 0.0) return uAlphaCutoff;
    // Screen-space footprint also accounts for zoom and oblique views. Preserve
    // the close-up mask, then recover diluted wires as filtering averages gaps.
    vec2 texels = vec2(textureSize(uBaseTexture, 0));
    vec2 dx = dFdx(vUV) * texels;
    vec2 dy = dFdy(vUV) * texels;
    float footprintSquared = max(dot(dx, dx), dot(dy, dy));
    float lod = 0.5 * log2(max(footprintSquared, 1.0));
    return mix(uAlphaCutoff, min(uAlphaCutoff, uMinifiedAlphaCutoff), smoothstep(1.0, 4.0, lod));
}

void main() {
    uint wound = 0u;
    if (!uShadowPass && vWoundBase > 0)
        wound = texelFetch(uZombieWounds, vWoundBase - 1 + uZombieTriangleOffset + gl_PrimitiveID).r;
    if ((wound & 1u) != 0u) discard;
    bool cavityInterior = !gl_FrontFacing && (wound & 2u) != 0u;
    if (!gl_FrontFacing && uZombieCullBackfaces && !cavityInterior) discard;
    if (uForestVisibility.z > 0.0) {
        vec2 offset = vWorldPosition.xz - uForestVisibility.xy;
        if (dot(offset, offset) > uForestVisibility.z * uForestVisibility.z) discard;
    }
    vec4 color = uBaseColor * vColor;
    if (uHasTexture && (!uShadowPass || uAlphaMode == 1)) color *= texture(uBaseTexture, vUV);
    if (cavityInterior) color = vec4(0.231, 0.00792, 0.0132, 1.0);
    if (uAlphaMode == 1 && color.a < alphaMaskCutoff()) discard;
    if (uShadowPass) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;
    float sunFacing = max(dot(normal, uSunDirection), 0.0);
    vec3 ambient = mix(uGroundAmbient, uSkyAmbient, normal.y * 0.5 + 0.5);
    float distanceFromEye = length(vWorldPosition - uEye);
    float canopyLight = mix(vCanopyLight.x, vCanopyLight.y, smoothstep(0.20, 0.95, vCanopyHeight));
    // Static canopy cover affects daylight only. Keep the night palette and
    // add the flashlight afterwards so the beam can illuminate forest interiors.
    vec3 light = uUnlit ? uUnlitLight * mix(canopyLight, 1.0, uNightBlend)
                        : ambient + uSunColor * sunFacing * sunlightVisibility(sunFacing, distanceFromEye);
    light += flashlight(normal);
    light += pointLight(normal, uMuzzleFlashPosition, uMuzzleFlashColor, uMuzzleFlashRange);
    for (int i = 0; i < uExplosionCount; ++i)
        light += pointLight(normal, uExplosionPositions[i], uExplosionColors[i], uExplosionRanges[i]);
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
