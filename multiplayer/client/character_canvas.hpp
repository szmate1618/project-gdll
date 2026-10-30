#pragma once
#include "model.hpp"
#include <array>
#include <optional>

namespace multiplayer {
// CPU painting surface. Six independent images avoid shared UVs and filtering
// across unrelated faces. Geometry and brush distances use runtime metres.
class CharacterCanvas {
public:
    static constexpr int resolution = 256;
    static constexpr int faceCount = 6;
    static constexpr float brushRadius = .045f;
    inline static constexpr std::array<std::uint8_t, 4> baseColor{70, 178, 190, 255};
    inline static constexpr std::array<std::uint8_t, 4> paintColor{235, 67, 112, 255};
    struct Surface { glm::vec3 origin, across, down, normal; };
    struct Hit { glm::vec3 position; int face; };
    CharacterCanvas();
    static const std::array<Surface, faceCount>& surfaces();
    static glm::vec3 surfacePoint(int face, glm::vec2 uv);
    static std::optional<Hit> intersect(glm::vec3 origin, glm::vec3 direction);
    // Tint the icon's alpha mask onto the fixed, square front-face location.
    void applyFace(const viewer::ImageData& icon);
    bool paint(glm::vec3 center);
    const std::array<viewer::ImageData, faceCount>& images() const { return images_; }
private:
    std::array<viewer::ImageData, faceCount> images_;
};
}
