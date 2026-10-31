#include "zombie_layer_physics.hpp"
#include "zombie_ragdoll_pose.hpp"
#include "zombie_melee_attack.hpp"
#include "physics_scene.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
void ZombieLayer::updateActiveAttacks(float seconds) {
    if (physics_) {
        // Cancellations, disconnected targets and normal clip completion all
        // release their rig before the next shared solve.
        auto it = physics_->attacks.begin();
        while (it != physics_->attacks.end()) {
            const auto id = *it++;
            const auto& instance = instances.at(id);
            if (instance.removed || !instance.attackClip || instance.ragdoll) stopActiveAttack(id);
        }
    }
    for (const auto& behavior : behaviors.entries()) {
        const auto id = behavior.zombie;
        auto& instance = instances.at(id);
        if (instance.removed || instance.ragdoll || !instance.attackClip) continue;
        const auto& asset = assets.at(instance.asset);
        const auto& clip = instance.attackClip == 1 ? asset.attackLeftClip : asset.attackRightClip;
        if (!clip || !hasZombieMeleeAttack(asset)) continue;
        ZombieInstance target;
        target.restTransform = instance.restTransform;
        target.activationPose = sampleAnimatedPose(asset, instance.attackSeconds, 0, &*clip);
        RagdollPose reference, desired;
        if (!makeZombieRagdollPoses(target, asset, reference, desired)) continue;
        if (!instance.activeRagdoll) {
            if (!physics_) physics_ = std::make_unique<Physics>();
            if (instance.attackRevision == std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error("Zombie attack revision exhausted");
            // Begin at the clip's first pose, then let the first solve follow
            // the current target. Never capture a stale rendered walk frame.
            target.activationPose = sampleAnimatedPose(asset, 0, 0, &*clip);
            RagdollPose initialReference, initial;
            makeZombieRagdollPoses(target, asset, initialReference, initial);
            const auto feet = glm::vec3(instance.restRoot[3]);
            const float yaw = std::atan2(instance.restRoot[2].x, instance.restRoot[0].x);
            const auto handle = physics_->world.create(feet, yaw, {}, initialReference, initial);
            try { physics_->attacks.insert(id); }
            catch (...) { physics_->world.destroy(handle); throw; }
            instance.ragdollHandle = handle;
            instance.activationPose = std::move(target.activationPose);
            instance.activeRagdoll = instance.physicsRagdoll = true;
            instance.ragdollSleeping = false;
            instance.displayedAnimationFrame = -1;
            ++instance.attackRevision;
            updateZombieRagdollSkin(instance, asset, physics_->world);
            ++instance.ragdollPoseVersion;
            markRenderChanged(id);
        }
        physics_->world.drivePose(instance.ragdollHandle, desired, seconds);
    }
}

void ZombieLayer::stopActiveAttack(std::size_t id) {
    if (!physics_ || !physics_->attacks.erase(id)) return;
    auto& instance = instances.at(id);
    physics_->world.destroy(instance.ragdollHandle);
    instance.activeRagdoll = instance.physicsRagdoll = false;
    instance.ragdollSleeping = false;
    instance.transform = instance.restTransform;
    instance.activationPose.clear();
    instance.ragdollBones.clear();
    instance.displayedAnimationFrame = -1;
    markRenderChanged(id);
    updateSpatialMembership(id);
}

void ZombieLayer::prepareAttackPhysics(PhysicsScene& scene) {
    if (!physics_) return;
    for (const auto id : physics_->attacks) {
        const auto feet = glm::vec3(instances.at(id).restRoot[3]);
        // Supported rigs cannot travel away from their root. Include floor
        // height even while both authored hands are above a flat scene's AABB.
        scene.ensureNear(feet - glm::vec3(3), feet + glm::vec3(3));
    }
}

std::optional<std::size_t> ZombieLayer::activeAttackOwner(b3BodyId body) const {
    if (physics_)
        for (const auto id : physics_->attacks)
            if (physics_->world.ownsBody(instances.at(id).ragdollHandle, body)) return id;
    return std::nullopt;
}
} // namespace viewer
