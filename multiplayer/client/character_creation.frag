#version 330 core
in vec3 vPosition;
in vec3 vNormal;
in vec2 vUV;
flat in int vFace;
uniform sampler2DArray uPaint;
uniform vec4 uBrush;
uniform bool uHover;
out vec4 color;
void main() {
    vec3 paint = texture(uPaint, vec3(vUV, vFace)).rgb;
    float light = .55 + .45 * max(dot(vNormal, normalize(vec3(-.4,.8,1))), 0);
    color = vec4(paint * light, 1);
    if (uHover && abs(distance(vPosition, uBrush.xyz) - uBrush.w) < .0025)
        color = vec4(1);
}
