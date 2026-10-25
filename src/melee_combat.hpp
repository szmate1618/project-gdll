#pragma once
#include <memory>
#include <cstddef>
#include <glm/glm.hpp>

namespace viewer {
class PhysicsWorld;
class PhysicsScene;
class CollisionWorld;
class PlayerBody;
struct ZombieLayer;

struct MeleeContactStats {
    std::size_t cachedBoneMeshes = 0, activeBoneBodies = 0, deformingMeshes = 0;
};

// Nearby skinned zombies use immutable bone-local triangle meshes, moved rigidly
// with the displayed skeleton or spring ragdoll. Non-skinned models retain the
// posed surface fallback. Contact
// impulses from the currently striking arm, rather than the aiming ray or idle
// arm contacts, initiate the spring ragdoll handoff.
class MeleeCombat {
public:
    explicit MeleeCombat(std::shared_ptr<PhysicsWorld> world);
    ~MeleeCombat();
    void prepare(PlayerBody& player, ZombieLayer* zombies, double animationSeconds);
    void finish(PlayerBody& player, ZombieLayer* zombies, double animationSeconds);
    // True only when a physical arm accepts the strike.
    bool punch(PlayerBody& player, glm::vec3 eye, glm::vec3 direction, const CollisionWorld& scene);
    // Shared proxies for all participating players; collect every solved hit
    // before changing any zombie ownership/contact surface. Borrows the array.
    void prepare(PlayerBody* const* players, std::size_t count, ZombieLayer* zombies, double animationSeconds);
    void finish(PlayerBody* const* players, std::size_t count, ZombieLayer* zombies, double animationSeconds);
    MeleeContactStats contactStats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
