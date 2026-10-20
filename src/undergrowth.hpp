#pragma once

#include "tree_layer.hpp"
#include "visibility.hpp"
#include "noise.hpp"
#include <array>
#include <optional>

namespace viewer {

struct UndergrowthMotion {
    glm::vec3 feet{0};
    float height = 1.8f, radius = .3f; // Current stance, meters, world Y up.
    bool grounded = true, running = false;
};
struct UndergrowthEvent {
    NoiseEvent noise;
    bool running = false;
    std::uint64_t variation = 0;
    float gainScale = 1; // Stance-dependent playback, independent of AI decibels.
};
struct UndergrowthContact {
    bool touched = false, dry = false;
    float distance = 0;
    glm::vec3 position{0};
    VisibilityStats stats;
};

// Immutable gameplay index, independent of render culling/LoD/blast animation.
// Local ellipse occupies 85% of the card width/depth; source transforms are
// applied once, including reflections/nonuniform scale. No rigid bodies.
class UndergrowthIndex {
public:
    UndergrowthIndex() = default;
    explicit UndergrowthIndex(const TreeLayer& trees);
    std::size_t size() const { return shrubs_.size(); }
    UndergrowthContact contact(const UndergrowthMotion& before, const UndergrowthMotion& after,
                              std::vector<std::size_t>& scratch) const;
private:
    struct Shrub { glm::mat4 inverse; glm::vec3 center, half; bool dry; };
    std::vector<Shrub> shrubs_;
    VisibilityIndex index_;
};

// One emitter per player. Accept actual movement only, never prediction replays.
// Batches swept paths at 20 Hz; emits at most four bursts/s and one per 0.65 m.
class UndergrowthEmitter {
public:
    explicit UndergrowthEmitter(std::uint32_t seed = 0) : seed_(seed) {}
    std::optional<UndergrowthEvent> update(float seconds, const UndergrowthMotion& before,
                                         const UndergrowthMotion& after, const UndergrowthIndex& index);
    void reset();
private:
    struct Segment { UndergrowthMotion before, after; };
    void clearPath();
    std::array<Segment, 64> path_{};
    std::size_t pathSize_ = 0;
    std::vector<std::size_t> scratch_;
    glm::vec3 lastFeet_{0};
    bool hasLast_ = false, touching_ = false;
    double querySeconds_ = 0, sinceEvent_ = 1;
    float idleSeconds_ = 0;
    float contactDistance_ = 0;
    std::uint64_t events_ = 0;
    std::uint32_t seed_;
};
} // namespace viewer
