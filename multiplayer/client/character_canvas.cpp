#include "character_canvas.hpp"
#include "player_dimensions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace multiplayer {
namespace {
std::size_t offset(int x, int y) { return (static_cast<std::size_t>(y) * CharacterCanvas::resolution + x) * 4; }
}
const std::array<CharacterCanvas::Surface, CharacterCanvas::faceCount>& CharacterCanvas::surfaces() {
    static const auto faces = [] {
        const float x = standingPlayerSize.x * .5f, h = standingPlayerSize.y, z = standingPlayerSize.z * .5f;
        return std::array<Surface, faceCount>{{
            {{-x,h,z}, {2*x,0,0}, {0,-h,0}, {0,0,1}},
            {{x,h,-z}, {-2*x,0,0}, {0,-h,0}, {0,0,-1}},
            {{x,h,z}, {0,0,-2*z}, {0,-h,0}, {1,0,0}},
            {{-x,h,-z}, {0,0,2*z}, {0,-h,0}, {-1,0,0}},
            {{-x,h,-z}, {2*x,0,0}, {0,0,2*z}, {0,1,0}},
            {{-x,0,z}, {2*x,0,0}, {0,0,-2*z}, {0,-1,0}}
        }};
    }();
    return faces;
}
glm::vec3 CharacterCanvas::surfacePoint(int face, glm::vec2 uv) {
    const auto& s = surfaces().at(static_cast<std::size_t>(face));
    return s.origin + s.across * uv.x + s.down * uv.y;
}
CharacterCanvas::CharacterCanvas() {
    for (auto& image : images_) {
        image.width = image.height = resolution;
        image.rgba.resize(resolution * resolution * 4);
        for (std::size_t i = 0; i < image.rgba.size(); i += 4)
            std::copy(baseColor.begin(), baseColor.end(), image.rgba.begin() + static_cast<std::ptrdiff_t>(i));
    }
}
std::optional<CharacterCanvas::Hit> CharacterCanvas::intersect(glm::vec3 origin, glm::vec3 direction) {
    const float length = glm::length(direction);
    if (!std::isfinite(length) || length < 1e-8f) return {};
    direction /= length;
    float nearest = std::numeric_limits<float>::max();
    std::optional<Hit> hit;
    for (int face = 0; face < faceCount; ++face) {
        const auto& s = surfaces()[face];
        const float divisor = glm::dot(direction, s.normal);
        if (std::abs(divisor) < 1e-7f) continue;
        const float distance = glm::dot(s.origin - origin, s.normal) / divisor;
        if (distance < 0 || distance >= nearest) continue;
        const auto point = origin + direction * distance;
        const auto delta = point - s.origin;
        const float u = glm::dot(delta, s.across) / glm::dot(s.across, s.across);
        const float v = glm::dot(delta, s.down) / glm::dot(s.down, s.down);
        if (u < 0 || u > 1 || v < 0 || v > 1) continue;
        nearest = distance; hit = Hit{point, face};
    }
    return hit;
}
void CharacterCanvas::applyFace(const viewer::ImageData& icon) {
    if (icon.width <= 0 || icon.height <= 0 ||
        icon.rgba.size() != static_cast<std::size_t>(icon.width) * icon.height * 4)
        throw std::runtime_error("Invalid character face image");
    // Square 42 cm stamp centered 1.45 m above the feet on the +Z face.
    constexpr float size = .42f, top = 1.45f + size * .5f;
    auto& destination = images_[0].rgba;
    for (int y = 0; y < resolution; ++y) for (int x = 0; x < resolution; ++x) {
        const auto p = surfacePoint(0, {(x + .5f) / resolution, (y + .5f) / resolution});
        const float u = (p.x + size * .5f) / size, v = (top - p.y) / size;
        if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
        const auto source = (static_cast<std::size_t>(int(v * icon.height)) * icon.width + int(u * icon.width)) * 4;
        const unsigned alpha = icon.rgba[source + 3];
        const auto target = offset(x, y);
        constexpr std::array<unsigned, 3> ink{18, 28, 36};
        for (int c = 0; c < 3; ++c)
            destination[target + c] = static_cast<std::uint8_t>((ink[c] * alpha + destination[target + c] * (255 - alpha) + 127) / 255);
    }
}
bool CharacterCanvas::paint(glm::vec3 center) {
    bool changed = false;
    for (int face = 0; face < faceCount; ++face) {
        const auto& s = surfaces()[face];
        const auto delta = center - s.origin;
        if (std::abs(glm::dot(delta, s.normal)) > brushRadius) continue;
        const float u = glm::dot(delta, s.across) / glm::dot(s.across, s.across);
        const float v = glm::dot(delta, s.down) / glm::dot(s.down, s.down);
        const float ru = brushRadius / glm::length(s.across), rv = brushRadius / glm::length(s.down);
        const int x0 = std::max(0, int(std::floor((u - ru) * resolution)));
        const int x1 = std::min(resolution - 1, int(std::floor((u + ru) * resolution)));
        const int y0 = std::max(0, int(std::floor((v - rv) * resolution)));
        const int y1 = std::min(resolution - 1, int(std::floor((v + rv) * resolution)));
        auto& pixels = images_[face].rgba;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const auto p = surfacePoint(face, {(x + .5f) / resolution, (y + .5f) / resolution});
            const auto difference = p - center;
            if (glm::dot(difference, difference) > brushRadius * brushRadius) continue;
            const auto pixel = offset(x, y);
            if (std::equal(paintColor.begin(), paintColor.end(), pixels.begin() + static_cast<std::ptrdiff_t>(pixel))) continue;
            std::copy(paintColor.begin(), paintColor.end(), pixels.begin() + static_cast<std::ptrdiff_t>(pixel));
            changed = true;
        }
    }
    return changed;
}
}
