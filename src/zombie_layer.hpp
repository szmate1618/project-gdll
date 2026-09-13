#pragma once

#include "animated_model.hpp"
#include "collision_world.hpp"

namespace viewer {

struct ZombieInstance {
    std::size_t asset = 0;
    glm::mat4 transform{1};
    float phase = 0; // Fraction of the idle cycle, independent of frame rate.
};

struct ZombieLayer {
    std::vector<AnimatedModel> assets;
    std::vector<ZombieInstance> instances;
    glm::vec3 boundsMin{0}, boundsMax{0};
};

bool hasZombieModels(const std::filesystem::path& source);

// Source is one animated glTF/GLB or a directory of individual characters.
// Character height is normalized to 1.8 m; feet use the rendered town surface.
ZombieLayer loadZombieLayer(const std::filesystem::path& source,
                           const CollisionWorld& world, const Model& scene,
                           glm::vec2 center, std::size_t count = 1000,
                           float radius = 100.0f);

} // namespace viewer
