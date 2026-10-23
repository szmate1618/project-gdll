#version 330 core
in vec2 vUV;
in float vOpacity;
flat in float vKind;
uniform sampler2D uBloodMask;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    float alpha = min(1.0,texture(uBloodMask,vUV).r*(vKind>0.0 ? 2.5 : 1.0))*vOpacity;
    if (vKind==0.0) alpha = min(1.0,alpha*1.3);
    if (alpha < .004) discard;
    vec3 color = uColor*(vKind==2.0 ? .55 : 1.0);
    vec3 srgb = mix(12.92*color,1.055*pow(color,vec3(1.0/2.4))-.055,
                    step(vec3(.0031308),color));
    fragColor = vec4(srgb,alpha);
}
