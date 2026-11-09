#include "zombie_layer_physics.hpp"
#include "physics_scene.hpp"

namespace viewer {

void ZombieLayer::prepareScenePhysics(PhysicsScene& scene) {
    if (!physics_) return;
    for (const auto id : ragdollIds_) {
        const auto& instance = instances[id];
        if (!instance.removed && instance.physicsRagdoll)
            physics_->world.prepareScenePhysics(instance.ragdollHandle, scene);
    }
}

std::vector<glm::vec3> ZombieLayer::physicsSupportPositions() const {
    std::vector<glm::vec3> positions;
    if (!physics_) return positions;
    positions.reserve(ragdollIds_.size() * ragdollPartCount);
    for (const auto id : ragdollIds_) {
        const auto& instance = instances[id];
        if (instance.removed || !instance.physicsRagdoll) continue;
        for (const auto& body : physics_->world.bodyTransforms(instance.ragdollHandle))
            positions.emplace_back(body[3]);
    }
    return positions;
}

} // namespace viewer
