#version 330 core
layout(location=0) in vec4 aCenterSize;
layout(location=1) in vec2 aRotationOpacity;
layout(location=2) in vec4 aVelocityAge;
layout(location=3) in vec4 aNormalKind;
uniform mat4 uView;
uniform mat4 uProjection;
out vec2 vUV;
out float vOpacity;
flat out float vKind;
void main() {
    const vec2 corners[6] = vec2[6](vec2(-.5,-.5),vec2(.5,-.5),vec2(.5,.5),
                                  vec2(-.5,-.5),vec2(.5,.5),vec2(-.5,.5));
    vec2 corner = corners[gl_VertexID];
    vUV = corner+.5;
    vOpacity = aRotationOpacity.y;
    vKind = aNormalKind.w;
    float c = cos(aRotationOpacity.x), s = sin(aRotationOpacity.x);
    vec2 rotated = vec2(c*corner.x-s*corner.y,s*corner.x+c*corner.y);
    vec3 point = aCenterSize.xyz;
    if (vKind==2.0) {
        vec3 n = aNormalKind.xyz;
        vec3 tangent = normalize(cross(n,abs(n.y)>.9 ? vec3(1,0,0) : vec3(0,1,0)));
        vec3 bitangent = cross(n,tangent);
        gl_Position = uProjection*uView*vec4(point+(tangent*rotated.x+bitangent*rotated.y)*aCenterSize.w,1);
        return;
    }
    float age = aVelocityAge.w;
    point += aVelocityAge.xyz*age-vec3(0,4.905*age*age,0);
    vec4 center = uView*vec4(point,1);
    if (vKind==1.0) {
        vec2 velocity = (mat3(uView)*(aVelocityAge.xyz-vec3(0,9.81*age,0))).xy;
        vec2 axis = length(velocity)>.001 ? normalize(velocity) : vec2(0,1);
        rotated = vec2(-axis.y,axis.x)*corner.x+axis*corner.y*2.5;
    }
    center.xy += rotated*aCenterSize.w;
    gl_Position = uProjection*center;
}
