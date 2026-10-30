#version 330 core
uniform sampler2D uScene;
uniform sampler2D uHistory;
uniform vec4 uBounds[18];
uniform vec2 uViewport;
uniform float uDeltaSeconds;
uniform bool uHistoryValid;
uniform vec2 uSectorRadii;
out float opacity;

vec3 linearColor(vec3 encoded) {
    return mix(encoded / 12.92, pow((encoded + 0.055) / 1.055, vec3(2.4)),
               greaterThan(encoded, vec3(0.04045)));
}

void main() {
    int group = int(gl_FragCoord.x);
    vec4 rectangle = uBounds[group];
    if (!(rectangle.z > 0.0 && rectangle.w > 0.0)) { opacity = 0.0; return; }
    vec2 first = max(rectangle.xy, vec2(0.0));
    vec2 last = min(rectangle.xy + rectangle.zw, uViewport);
    if (any(lessThanEqual(last, first))) { opacity = 0.0; return; }

    // Panels sample a rectangular grid; each sector samples its own annulus.
    // Squared-radius strata give equal-area samples across the curved choice.
    float luminance = 0.0;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        vec2 fraction = (vec2(x, y) + 0.5) / 8.0;
        vec2 point;
        if (group >= 15) {
            float angle = radians(float(group - 15) * 120.0 - 60.0 + fraction.x * 120.0);
            float radius = sqrt(mix(uSectorRadii.x * uSectorRadii.x, uSectorRadii.y * uSectorRadii.y, fraction.y)) *
                           min(rectangle.z, rectangle.w);
            point = rectangle.xy + rectangle.zw * 0.5 + vec2(sin(angle), -cos(angle)) * radius;
        } else point = mix(first, last, fraction);
        vec2 uv = vec2(point.x / uViewport.x, 1.0 - point.y / uViewport.y);
        if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
            luminance += dot(linearColor(texture(uScene, uv).rgb), vec3(0.2126, 0.7152, 0.0722));
    }
    float target = 0.195 * smoothstep(0.12, 0.55, luminance / 64.0);
    opacity = target;
    if (uHistoryValid) {
        float previous = texelFetch(uHistory, ivec2(group, 0), 0).r;
        float seconds = target > previous ? 0.10 : 0.30;
        opacity = mix(previous, target, 1.0 - exp(-uDeltaSeconds / seconds));
        if (target == 0.0 && opacity < 0.0001) opacity = 0.0;
    }
}
