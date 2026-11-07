#include "blood_spurt.hpp"
#include <cmath>

namespace viewer {
void BloodSpurts::emit(BloodSpurtStart start, double seconds) {
    start.id = nextId_++;
    if (!nextId_) nextId_ = 1;
    start.age = 0;
    entries_[slot_] = {start,seconds};
    slot_ = (slot_+1)%capacity;
}
std::vector<BloodSpurtStart> BloodSpurts::recent(double seconds) const {
    std::vector<BloodSpurtStart> result;
    for (const auto& entry : entries_) {
        const auto age = seconds-entry.seconds;
        if (!entry.start.id || !std::isfinite(age) || age<0 || age>=historySeconds) continue;
        auto start = entry.start;
        start.age = static_cast<float>(age);
        result.push_back(start);
    }
    return result;
}
} // namespace viewer
