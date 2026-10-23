#include "blood_mist.hpp"

#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace viewer {
void BloodMist::emit(glm::vec3 point, BloodMistSize size, double seconds) {
    if (!std::isfinite(seconds) || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) return;
    entries_[slot_] = {{nextId_++,point,0,size},seconds};
    if (!nextId_) nextId_ = 1;
    slot_ = (slot_+1)%capacity;
}
std::vector<BloodMistBurst> BloodMist::sample(double seconds) const {
    auto result = recent(seconds);
    result.erase(std::remove_if(result.begin(),result.end(),[](const auto& burst) {
        return burst.age>=duration;
    }),result.end());
    return result;
}
std::vector<BloodMistBurst> BloodMist::recent(double seconds) const {
    std::vector<BloodMistBurst> result;
    for (const auto& entry : entries_) {
        const double age = seconds-entry.seconds;
        if (!entry.burst.id || !std::isfinite(age) || age < 0 || age >= historySeconds) continue;
        auto burst = entry.burst;
        burst.age = static_cast<float>(age);
        result.push_back(burst);
    }
    return result;
}
void BloodMist::replace(const std::vector<BloodMistBurst>& bursts, double snapshotSeconds) {
    if (bursts.size()>capacity) throw std::runtime_error("Too many blood mist bursts");
    entries_ = {};
    slot_ = 0;
    for (const auto& burst : bursts) {
        entries_[slot_] = {burst,snapshotSeconds-burst.age};
        slot_ = (slot_+1)%capacity;
    }
}
} // namespace viewer
