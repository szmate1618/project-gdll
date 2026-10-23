#version 330 core
in vec2 vUV;
in float vOpacity;
uniform sampler2D uBloodMask;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    float alpha = texture(uBloodMask,vUV).r*vOpacity;
    if (alpha < .004) discard;
    vec3 srgb = mix(12.92*uColor,1.055*pow(uColor,vec3(1.0/2.4))-.055,
                    step(vec3(.0031308),uColor));
    fragColor = vec4(srgb,alpha);
}
