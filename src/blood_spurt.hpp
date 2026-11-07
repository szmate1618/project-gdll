#pragma once

#include "blood_particles.hpp"
#include <vector>

namespace viewer {
// One cosmetic start, in world metres, with an optional bone-local wound.
// Droplets, cached floor planes and stains never cross the network.
struct BloodSpurtStart {
    std::uint32_t id = 0;
    glm::vec3 position{0}, direction{0,1,0};
    float age = 0;
    BloodAttachment attachment;
};
class BloodSpurts {
public:
    static constexpr std::size_t capacity = 128;
    static constexpr float historySeconds = 1.8f;
    void emit(BloodSpurtStart start, double seconds);
    std::vector<BloodSpurtStart> recent(double seconds) const;
private:
    struct Entry { BloodSpurtStart start; double seconds = 0; };
    std::array<Entry,capacity> entries_{};
    std::size_t slot_ = 0;
    std::uint32_t nextId_ = 1;
};
} // namespace viewer
