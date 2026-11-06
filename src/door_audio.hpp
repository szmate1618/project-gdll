#pragma once

#include <glm/glm.hpp>
#include "door_slam.hpp"
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {
struct DoorAudioSample {
    std::size_t id;
    float angle; // Hinge radians; zero closed, positive outward.
    glm::vec3 position;
    bool broken = false; // A released restraint has no closed stop to slam against.
};
// Observes physics poses; never changes a hinge, contact or interaction.
class DoorAudioTracker {
public:
    std::vector<DoorSlamEvent> update(float seconds, const std::vector<DoorAudioSample>& doors);
private:
    std::unordered_map<std::size_t, DoorSlamState> states_;
};
} // namespace viewer
