#version 330 core
uniform vec4 uBounds[24];
uniform vec2 uViewport;
uniform float uPadding;
uniform bool uSectorPass;
uniform vec2 uSectorRadii;
uniform vec2 uPageRadii;
uniform float uSectorSegments;
flat out int vGroup;
void main() {
    vGroup = gl_InstanceID + (uSectorPass ? 15 : 0);
    vec4 rectangle = uBounds[vGroup];
    vec2 point;
    if (uSectorPass) {
        float step = float(gl_VertexID / 2) / uSectorSegments;
        bool page = gl_InstanceID >= 4;
        float angle = radians(page ? float(gl_InstanceID - 4) * 72.0 - 35.0 + step * 70.0 :
                                     float(gl_InstanceID) * 90.0 - 45.0 + step * 90.0);
        vec2 radii = page ? uPageRadii : uSectorRadii;
        float radius = ((gl_VertexID & 1) == 0 ? radii.x : radii.y) * min(rectangle.z, rectangle.w);
        point = rectangle.xy + rectangle.zw * 0.5 + vec2(sin(angle), -cos(angle)) * radius;
    } else {
        vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
        point = rectangle.xy - uPadding + corner * (rectangle.zw + 2.0 * uPadding);
    }
    gl_Position = vec4(point.x / uViewport.x * 2.0 - 1.0, 1.0 - point.y / uViewport.y * 2.0, 0.0, 1.0);
    if (!(rectangle.z > 0.0 && rectangle.w > 0.0)) gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
}
