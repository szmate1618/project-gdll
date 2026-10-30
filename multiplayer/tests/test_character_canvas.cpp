#include "client/character_canvas.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::array<std::uint8_t, 4> pixel(const multiplayer::CharacterCanvas& canvas, int face, int x, int y) {
    const auto& image = canvas.images()[face];
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return {image.rgba[offset], image.rgba[offset+1], image.rgba[offset+2], image.rgba[offset+3]};
}
void check() {
    using Canvas = multiplayer::CharacterCanvas;
    // Known world-space points, independent of the surface parametrization.
    const std::array<glm::vec3, 6> centers{{{0,.9f,.2f}, {0,.9f,-.2f}, {.3f,.9f,0},
        {-.3f,.9f,0}, {0,1.8f,0}, {0,0,0}}};
    const std::array<glm::vec3, 6> normals{{{0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}}};
    for (int face = 0; face < 6; ++face) {
        const auto hit = Canvas::intersect(centers[face] + normals[face] * 3.f, -normals[face] * 7.f);
        require(hit && hit->face == face && glm::distance(hit->position, centers[face]) < 1e-5f,
            "Ray failed to select the nearest face at the standing player's dimensions");
        Canvas canvas;
        require(canvas.paint(centers[face]), "Brush did not paint the hit surface");
        require(pixel(canvas, face, 128, 128) == Canvas::paintColor, "Hit face center was not painted");
        require(pixel(canvas, face, 1, 1) == Canvas::baseColor, "Brush reached a distant corner");
    }
    require(!Canvas::intersect({1,.9f,3}, {0,0,-1}), "A ray beside the box hit it");
    require(!Canvas::intersect({0,.9f,3}, {0,0,1}), "A ray pointing away hit the box behind it");
    require(!Canvas::intersect({0,2,0}, {1,0,0}), "Parallel ray above the box hit it");
    require(!Canvas::intersect({0,.9f,3}, {0,0,0}), "Zero direction produced a hit");
    Canvas canvas;
    require(!canvas.paint({2,2,2}), "Sphere away from the box painted a surface");
    canvas.paint({0,.9f,.2f});
    // A 4.5 cm radius includes these points but excludes points about 7 cm away
    // on both the short horizontal and tall vertical texture axes.
    require(pixel(canvas, 0, 140,128) == Canvas::paintColor, "Horizontal brush radius is too small");
    require(pixel(canvas, 0, 128,132) == Canvas::paintColor, "Vertical brush radius is too small");
    require(pixel(canvas, 0, 158,128) == Canvas::baseColor, "Horizontal brush radius is too large");
    require(pixel(canvas, 0, 128,138) == Canvas::baseColor, "Vertical brush radius is too large");
    require(pixel(canvas, 1, 128,128) == Canvas::baseColor, "Painting front leaked onto back through shared UVs");
    Canvas corner;
    corner.paint({.3f,.9f,.2f});
    require(pixel(corner, 0, 255,128) == Canvas::paintColor, "Sphere failed at the front edge");
    require(pixel(corner, 2, 0,128) == Canvas::paintColor, "Sphere did not wrap onto the adjoining side");
    require(pixel(corner, 3, 128,128) == Canvas::baseColor, "Corner sphere painted the opposite side");
    // Only the source's top-left quadrant is opaque. The face must remain
    // upright and square in metres despite the tall front surface's UV scale.
    viewer::ImageData icon; icon.width = icon.height = 2; icon.rgba.resize(16, 0);
    icon.rgba[3] = 255;
    Canvas stamped; stamped.applyFace(icon);
    require(pixel(stamped, 0, 64,32) != Canvas::baseColor, "Face's top-left quadrant is missing or flipped");
    require(pixel(stamped, 0, 192,32) == Canvas::baseColor, "Transparent face quadrant overwrote the base");
    require(pixel(stamped, 0, 64,128) == Canvas::baseColor, "Face was stretched down the body");
    require(pixel(stamped, 1, 64,32) == Canvas::baseColor, "Front face stamp appeared on the back");
    const Canvas::Color green{30, 200, 50, 255}, red{240, 30, 20, 255}, yellow{240, 220, 20, 255};
    stamped.setBrushColor(green); stamped.paint({0,.9f,.2f});
    stamped.setBaseColor(red); stamped.setFaceColor(yellow);
    require(pixel(stamped, 0, 128,128) == green, "Changing base/icon colors must preserve brush strokes");
    require(pixel(stamped, 1, 128,128) == red, "Changing the base must recolor unpainted sides");
    require(pixel(stamped, 0, 64,32) == yellow, "Face icon must use the selected tint");
    viewer::ImageData transparent = icon; transparent.rgba.assign(16, 0);
    stamped.applyFace(transparent);
    require(pixel(stamped, 0, 64,32) == red, "Changing icons must remove the previous stamp");
    require(pixel(stamped, 0, 128,128) == green, "Changing icons must preserve brush strokes");
    stamped.setBrushColor(yellow); stamped.paint({0,.5f,.2f});
    require(pixel(stamped, 0, 128,128) == green, "Brush color changes must not recolor old strokes");
    require(pixel(stamped, 0, 128,185) == yellow, "New strokes must use the selected brush color");
    stamped.reset();
    require(pixel(stamped, 0, 128,128) == Canvas::baseColor && stamped.brush() == Canvas::paintColor && stamped.ink() == Canvas::faceColor,
            "Reset must clear paint and restore the default colors");
}
}
int main() {
    try { check(); std::cout << "Character ray, dimensions, spherical brush, seams and face placement passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
