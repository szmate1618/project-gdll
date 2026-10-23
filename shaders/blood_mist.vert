#version 330 core
layout(location=0) in vec4 aCenterSize;
layout(location=1) in vec2 aRotationOpacity;
uniform mat4 uView;
uniform mat4 uProjection;
out vec2 vUV;
out float vOpacity;
void main() {
    const vec2 corners[6] = vec2[6](vec2(-.5,-.5),vec2(.5,-.5),vec2(.5,.5),
                                  vec2(-.5,-.5),vec2(.5,.5),vec2(-.5,.5));
    vec2 corner = corners[gl_VertexID];
    vUV = corner+.5;
    vOpacity = aRotationOpacity.y;
    float c = cos(aRotationOpacity.x), s = sin(aRotationOpacity.x);
    vec2 rotated = vec2(c*corner.x-s*corner.y,s*corner.x+c*corner.y);
    vec4 center = uView*vec4(aCenterSize.xyz,1);
    center.xy += rotated*aCenterSize.w;
    gl_Position = uProjection*center;
}
