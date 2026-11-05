#pragma once

#include "weapon_config.hpp"
#include <cstdint>
#include <random>

namespace viewer {
struct FPSConfig;

// Per-axis normal standard deviation in degrees. Speed is actual horizontal
// travel in m/s; posture is continuous: 0 standing, 1 crouched, 2 prone.
float hipFireInaccuracy(const WeaponConfig& weapon, float speed, float posture,
                       const FPSConfig& movement, bool scoped);

class ShotInaccuracy {
public:
    explicit ShotInaccuracy(std::uint32_t seed = std::random_device{}()) : random_(seed) {}
    // Sample once per trigger event, before generating any shotgun pellets.
    glm::vec2 sample(float standardDeviationDegrees);
private:
    std::mt19937 random_;
    std::normal_distribution<float> normal_{0, 1};
};

// Camera-local yaw/right and pitch/up offsets, shared by the whole shot.
glm::vec3 inaccurateShotDirection(glm::vec3 forward, glm::vec2 offsetDegrees);
// A circle containing 95% of the normal shot centers, in vertical NDC units.
float inaccuracyCrosshairRadius(float standardDeviationDegrees, float projectionY);
} // namespace viewer
