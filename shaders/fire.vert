#version 330 core
layout(location=0) in vec4 aPointAge;
layout(location=1) in vec2 aSizeSeed;
uniform mat4 uProjectionView;
uniform vec3 uRight;
out vec2 vUV;
flat out float vAge;
flat out float vSeed;
void main() {
    const vec2 corners[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));
    vUV = corners[gl_VertexID];
    vAge = aPointAge.w;
    vSeed = aSizeSeed.y*37.0;
    float size = aSizeSeed.x;
    vec3 point = aPointAge.xyz+uRight*(vUV.x-.5)*size+vec3(0,vUV.y*size*1.45,0);
    gl_Position = uProjectionView*vec4(point,1);
}
