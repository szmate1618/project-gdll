#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUv;
uniform vec2 uViewport;
uniform vec2 uTranslation;
uniform bool uMinimap;
uniform vec2 uMinimapSize;
uniform bool uMinimapRoads;
uniform vec2 uMinimapCenter;
uniform float uMinimapSpan;
out vec4 vColor;
out vec2 vUv;
void main() {
    vec2 point = uMinimapRoads ? (aPosition - uMinimapCenter) / uMinimapSpan + 0.5 : aPosition;
    vec2 pixel = ((uMinimap || uMinimapRoads) ? point * uMinimapSize : point) + uTranslation;
    gl_Position = vec4(pixel.x / uViewport.x * 2.0 - 1.0, 1.0 - pixel.y / uViewport.y * 2.0, 0.0, 1.0);
    vColor = aColor;
    vUv = uMinimapRoads ? point : aUv;
}
