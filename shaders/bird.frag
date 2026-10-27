#version 330 core
in vec2 vCard;
flat in int vPlane;
flat in float vFlap;
out vec4 fragColor;

void main() {
    bool silhouette;
    if (vPlane == 0) {
        // Top: a single black triangle, nose forward along local +Y.
        silhouette = vCard.y >= -.3 && vCard.y <= .48
                     && abs(vCard.x) <= (.48 - vCard.y) * (.48 / .78);
    } else if (vPlane == 1) {
        // Side: a long, very flat diamond.
        silhouette = abs(vCard.x) / .46 + abs(vCard.y) / .065 <= 1.0;
    } else {
        // Front: a round head/body and a flat wing diamond.
        float wingHeight = .045 + .025 * (vFlap + 1.0);
        silhouette = length(vCard - vec2(0, .025)) <= .085
                     || abs(vCard.x) / .48 + abs(vCard.y) / wingHeight <= 1.0;
    }
    if (!silhouette) discard;
    fragColor = vec4(.008, .009, .012, 1);
}
