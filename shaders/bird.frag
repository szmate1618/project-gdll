#version 330 core
in vec2 vUv;
in vec3 vNormal;
uniform sampler2D uAtlas;
uniform vec3 uDayNightTint;
out vec4 fragColor;
void main() {
    vec4 color = texture(uAtlas, vUv);
    if (color.a < .5) discard;
    vec3 normal = normalize(vNormal);
    if (!gl_FrontFacing) normal = -normal;
    float shade = .45 + .55 * max(dot(normal, normalize(vec3(-.3, .85, .5))), 0);
    fragColor = vec4(color.rgb * shade * uDayNightTint, 1);
}
