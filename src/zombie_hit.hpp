#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace viewer {

struct AnimatedModel;
struct ZombieInstance;

struct ZombieHitSphere {
    glm::vec3 center;
    float radius;
};

// World-meter head bounds from the displayed pose, including hair and child
// joints. Unmapped/custom rigs use the standard 1.8 m character's head region.
ZombieHitSphere zombieHeadHitSphere(const AnimatedModel& asset, const ZombieInstance& instance,
                                   const std::vector<glm::mat4>& pose);

} // namespace viewer
