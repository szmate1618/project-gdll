#pragma once
#include "fire_effects.hpp"
#include "collision_world.hpp"

namespace viewer {
// Cache one two-metre static BVH query per splash; never scan the town/forest.
class FireSurfaceQuery {
public:
    FireSurfaceQuery(const FireScene& scene, glm::vec3 center);
    std::optional<FireContact> cast(glm::vec3 origin, glm::vec3 direction, float reach) const;
private:
    const FireScene& scene_;
    std::vector<StaticCollisionFace> faces_;
    std::vector<NearbyTrunk> trunks_;
    struct Actor { std::size_t id; std::vector<glm::mat4> pose; };
    std::vector<Actor> zombies_;
};
} // namespace viewer
