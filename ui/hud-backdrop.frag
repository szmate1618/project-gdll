#version 330 core
uniform sampler2D uOpacity;
uniform vec3 uColor;
flat in int vGroup;
out vec4 fragColor;
void main() {
    float alpha = texelFetch(uOpacity, ivec2(vGroup, 0), 0).r;
    // The HUD blends encoded colors with premultiplied alpha.
    fragColor = vec4(uColor * alpha, alpha);
}
