#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace viewer {
enum class BloodMistSize : std::uint8_t { small, medium, large };
struct BloodMistBurst {
    std::uint32_t id = 0;
    glm::vec3 position{0}; // World meters.
    float age = 0; // Seconds since impact, also used by snapshots.
    BloodMistSize size = BloodMistSize::small;
};

// Bounded presentation history, independent of health and corpse ownership.
class BloodMist {
public:
    static constexpr std::size_t capacity = 128;
    static constexpr float duration = .2f;
    static constexpr float historySeconds = 1.0f; // Retain events across snapshot interpolation/loss.
    void emit(glm::vec3 point, BloodMistSize size, double seconds);
    std::vector<BloodMistBurst> sample(double seconds) const;
    std::vector<BloodMistBurst> recent(double seconds) const;
    void replace(const std::vector<BloodMistBurst>& bursts, double snapshotSeconds);
private:
    struct Entry { BloodMistBurst burst; double seconds = 0; };
    std::array<Entry,capacity> entries_{};
    std::size_t slot_ = 0;
    std::uint32_t nextId_ = 1;
};
} // namespace viewer
