#include "door_audio.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace viewer {
std::vector<glm::vec3> DoorAudioTracker::update(float seconds, const std::vector<DoorAudioSample>& doors) {
    std::vector<glm::vec3> slams;
    if (!std::isfinite(seconds) || seconds <= 0) return slams;
    std::unordered_set<std::size_t> present;
    for (const auto& door : doors) {
        if (!std::isfinite(door.angle) || !present.insert(door.id).second) continue;
        const float angle = std::abs(door.angle); // Inward hinges use negative angles.
        auto inserted = states_.try_emplace(door.id);
        auto& state = inserted.first->second;
        if (inserted.second) { state.angle = angle; state.armed = angle >= .12f; continue; }
        state.cooldown = std::max(0.f, state.cooldown - seconds);
        const float closingSpeed = (state.angle - angle) / seconds;
        if (angle >= .12f) state.armed = true;
        if (angle <= .035f && state.angle > .035f) {
            // Physics can clamp the final sample to the stop; retain the speed
            // immediately before impact as well as this frame's average.
            if (state.armed && state.cooldown == 0 && std::max(closingSpeed, state.closingSpeed) >= .8f) {
                slams.push_back(door.position);
                state.cooldown = .4f;
            }
            state.armed = false;
        }
        state.angle = angle;
        state.closingSpeed = closingSpeed;
    }
    for (auto it = states_.begin(); it != states_.end();)
        if (!present.count(it->first)) it = states_.erase(it);
        else ++it;
    return slams;
}
} // namespace viewer
