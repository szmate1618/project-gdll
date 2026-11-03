#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace viewer {
class CollisionWorld;

// Static same-storey walking space. Ramp footprints and incoming floor openings
// are excluded; ramp endpoints are reached through separate landing anchors.
struct HouseNavigationRoom {
    std::vector<glm::vec2> polygon;
    std::vector<std::vector<glm::vec2>> blocked;
    float floor = 0;
    bool walkable(glm::vec2 point) const;
    bool clearSegment(glm::vec2 from, glm::vec2 to) const;
    std::optional<glm::vec3> center(const std::vector<glm::vec2>& anchors, const CollisionWorld& world) const;
    std::vector<glm::vec3> path(glm::vec2 from, glm::vec2 to) const;
};

} // namespace viewer
