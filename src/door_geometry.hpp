#pragma once

#include "model.hpp"
#include <glm/gtc/quaternion.hpp>

namespace viewer {
struct DoorPanel {
    std::size_t draw = 0;
    glm::mat4 closedTransform{1};
    glm::vec3 hinge{0}, center{0}, halfExtents{0}, outward{0};
    glm::quat rotation{1, 0, 0, 0};
};

// Split generated *_doors draws into independent panels, retaining materials,
// source node transforms and LoD groups. Descriptors use runtime world meters.
// Call once before constructing renderer/visibility/collision caches.
std::vector<DoorPanel> prepareDoorPanels(Model& model);
} // namespace viewer
