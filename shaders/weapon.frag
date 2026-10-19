#version 330 core
in vec3 vColor;
in vec3 vEyePosition;
in vec3 vEyeNormal;
in vec3 vBaseColor;
uniform vec3 uMuzzleFlashPosition;
uniform vec3 uMuzzleFlashColor;
uniform float uMuzzleFlashRange;
out vec4 fragColor;
void main() {
    vec3 toLight = uMuzzleFlashPosition - vEyePosition;
    float distanceFromLight = length(toLight);
    float rangeFade = 1.0 - smoothstep(uMuzzleFlashRange * 0.75, uMuzzleFlashRange, distanceFromLight);
    float facing = max(dot(normalize(vEyeNormal), toLight / max(distanceFromLight, 0.001)), 0.0);
    // A small scattered fill represents the flame's extent and local bounce
    // on the eye-facing surfaces, which face away from the muzzle point.
    facing = 0.12 + 0.88 * facing;
    vec3 flash = uMuzzleFlashColor * (rangeFade * facing / (1.0 + distanceFromLight * distanceFromLight));
    fragColor = vec4(vColor + vBaseColor * flash, 1.0);
}
