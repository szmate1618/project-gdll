#include "door_audio.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace viewer {
std::vector<DoorSlamEvent> DoorAudioTracker::update(float seconds, const std::vector<DoorAudioSample>& doors) {
    std::vector<DoorSlamEvent> slams;
    if (!std::isfinite(seconds) || seconds <= 0) return slams;
    std::unordered_set<std::size_t> present;
    for (const auto& door : doors) {
        if (!std::isfinite(door.angle) || !present.insert(door.id).second) continue;
        if (door.broken) { states_.erase(door.id); continue; }
        if (const auto slam = sampleDoorSlam(states_[door.id], seconds, door.angle, door.position))
            slams.push_back(*slam);
    }
    for (auto it = states_.begin(); it != states_.end();)
        if (!present.count(it->first)) it = states_.erase(it);
        else ++it;
    return slams;
}
} // namespace viewer
