#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {
struct DoorAudioSample {
    std::size_t id;
    float angle; // Hinge radians; zero closed, positive outward.
    glm::vec3 position;
};
// Observes physics poses; never changes a hinge, contact or interaction.
class DoorAudioTracker {
public:
    std::vector<glm::vec3> update(float seconds, const std::vector<DoorAudioSample>& doors);
private:
    struct State { float angle = 0, closingSpeed = 0, cooldown = 0; bool armed = false; };
    std::unordered_map<std::size_t, State> states_;
};
} // namespace viewer
