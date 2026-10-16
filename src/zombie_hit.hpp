#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <vector>

namespace viewer {

struct AnimatedModel;
struct ZombieInstance;

struct ZombieHitSphere {
    glm::vec3 center;
    float radius;
};

struct ZombieHitBounds { glm::vec3 minimum{0}, maximum{0}; };
struct ZombieHeadVertex { std::size_t primitive = 0, vertex = 0; };
// Immutable per-asset preparation. Point bounds cover every idle/walk/run bone
// sample; linear matrix interpolation stays inside their union, including wrap.
struct ZombieHitGeometry {
    int head = -1;
    std::vector<ZombieHeadVertex> headVertices;
    ZombieHitBounds headPoints;
};
ZombieHitGeometry prepareZombieHitGeometry(const AnimatedModel& asset);
ZombieHitBounds zombieShotBounds(const ZombieInstance& instance, const ZombieHitGeometry& geometry);
// Conservative forward-ray broad phase; direction need not be normalized.
bool zombieRayIntersectsBounds(glm::vec3 origin, glm::vec3 direction, const ZombieHitBounds& bounds);

// World-meter head bounds from the displayed pose, including hair and child
// joints. Unmapped/custom rigs use the standard 1.8 m character's head region.
ZombieHitSphere zombieHeadHitSphere(const AnimatedModel& asset, const ZombieInstance& instance,
                                   const std::vector<glm::mat4>& pose, const ZombieHitGeometry& geometry);
// Convenience for unprepared callers; repeated queries should reuse geometry.
ZombieHitSphere zombieHeadHitSphere(const AnimatedModel& asset, const ZombieInstance& instance,
                                   const std::vector<glm::mat4>& pose);

} // namespace viewer
