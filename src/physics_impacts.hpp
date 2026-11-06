#pragma once

#include "acoustics.hpp"
#include "noise.hpp"
#include <box3d/id.h>
#include <cstdint>
#include <map>
#include <string_view>
#include <unordered_map>

namespace viewer {
struct PhysicsImpact { ImpactSound sound; float noiseDb; NoiseAudience audience = NoiseAudience::everyone; };

// Reads transient Box3D hits synchronously after each solve. No audio/device
// calls, workers, population scans or unbounded contact history.
class PhysicsImpacts {
public:
    explicit PhysicsImpacts(b3WorldId world, const std::filesystem::path& storage = {});
    void track(b3BodyId body, std::string_view profile, float scale = 1);
    void trackZombieImpact(b3BodyId body); // Living attack hands; player-only material impacts.
    void untrack(b3BodyId body);
    void capture();
    std::vector<PhysicsImpact> take();
    // Gameplay events are independent of the 64-event playback budget.
    std::vector<NoiseEvent> takeNoises();
private:
    struct Body { AcousticConfig config; float scale; std::uint64_t lastHit = 0; bool zombie = false; };
    b3WorldId world_;
    std::map<std::string, AcousticConfig, std::less<>> profiles_;
    std::unordered_map<std::uint64_t, Body> bodies_;
    std::vector<PhysicsImpact> events_;
    std::vector<NoiseEvent> noises_;
    std::uint64_t tick_ = 0;
};
} // namespace viewer
