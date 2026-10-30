#version 330 core
uniform vec4 uBounds[18];
uniform vec2 uViewport;
uniform float uPadding;
uniform bool uSectorPass;
uniform vec2 uSectorRadii;
uniform float uSectorSegments;
flat out int vGroup;
void main() {
    vGroup = gl_InstanceID + (uSectorPass ? 15 : 0);
    vec4 rectangle = uBounds[vGroup];
    vec2 point;
    if (uSectorPass) {
        float step = float(gl_VertexID / 2) / uSectorSegments;
        float angle = radians(float(gl_InstanceID) * 120.0 - 60.0 + step * 120.0);
        float radius = ((gl_VertexID & 1) == 0 ? uSectorRadii.x : uSectorRadii.y) * min(rectangle.z, rectangle.w);
        point = rectangle.xy + rectangle.zw * 0.5 + vec2(sin(angle), -cos(angle)) * radius;
    } else {
        vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
        point = rectangle.xy - uPadding + corner * (rectangle.zw + 2.0 * uPadding);
    }
    gl_Position = vec4(point.x / uViewport.x * 2.0 - 1.0, 1.0 - point.y / uViewport.y * 2.0, 0.0, 1.0);
    if (!(rectangle.z > 0.0 && rectangle.w > 0.0)) gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
}
