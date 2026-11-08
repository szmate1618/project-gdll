#pragma once

#include "house_layout.hpp"
#include <memory>

namespace tinygltf { class Value; }
namespace viewer {
// Prepared map metadata. Horizontal coordinates stay in the author's local
// metre frame; origin/bases and vertical datums are transformed once on load.
struct AuthoredFloorplan {
    glm::vec2 origin{0}, u{1, 0}, v{0, 1};
    float floor = 0, ceiling = 0;
    std::vector<float> upperFloors;
    std::vector<glm::vec2> footprint;
    house_layout::Layout layout;
    std::vector<std::string> labels;
};
std::shared_ptr<const AuthoredFloorplan> readAuthoredFloorplan(const tinygltf::Value& value, const glm::mat4& transform);
std::optional<HouseFloorplan> generateAuthoredFloorplan(const AuthoredFloorplan& prepared,
    const HouseSource& source, const std::vector<DoorPanel>& entrances);
} // namespace viewer
