#pragma once
#include "animated_model.hpp"
#include <filesystem>
#include <optional>

namespace viewer {
// Distances are meters. Rendering policy never changes AI, collision or physics.
struct ZombieDetail {
    std::size_t level = 0;
    float animationHz = 0; // Zero keeps continuous interpolation.
    bool visible = true;
};
ZombieDetail zombieCameraDetail(float distance, float projectionScale, bool enabled = true,
                               float maximumDistance = 180);
// Conservative world-space radius for early camera rejection; null means
// unlimited. Exact mesh/animation selection remains zombieCameraDetail's job.
std::optional<float> zombieCameraDrawRadius(float projectionScale, bool enabled, float maximumDistance);
std::vector<AnimatedModel> loadZombieLods(const std::filesystem::path& basePath, const AnimatedModel& base);
}
