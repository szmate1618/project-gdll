#version 330 core
in vec2 vUV;
flat in float vAge;
flat in float vSeed;
out vec4 fragColor;
float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
void main() {
    float rise = vAge*3.0;
    vec2 p = vec2(vUV.x*4.0+vSeed,vUV.y*5.0-rise);
    float turbulence = noise(p)*.65+noise(p*2.1)*.35;
    float sway = sin(vUV.y*7.0-vAge*6.0+vSeed)*.08*vUV.y;
    float width = (.48-vUV.y*.43)*(0.65+.35*turbulence);
    float body = 1.0-smoothstep(width*.35,width,abs(vUV.x-.5+sway));
    float top = 1.0-smoothstep(.3+turbulence*.45,.95,vUV.y);
    float envelope = smoothstep(0.0,.18,vAge)*(1.0-smoothstep(9.0,10.0,vAge));
    float alpha = body*top*envelope*.5;
    if (alpha<.004) discard;
    float core = (1.0-vUV.y)*body;
    // Emissive flame, independent of ambient illumination; additive blending.
    vec3 color = mix(vec3(1.0,.055,.003),vec3(1.0,.65,.08),pow(core,3.0));
    fragColor = vec4(color,alpha);
}
