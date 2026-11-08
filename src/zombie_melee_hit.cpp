#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_layer_physics.hpp"

#include <cmath>
#include <limits>

namespace viewer {
std::optional<ZombieJointContact> ZombieLayer::traceMeleeJoint(std::size_t id, glm::vec3 start, glm::vec3 end,
                                                             double seconds) const {
    if (id >= instances.size() || !std::isfinite(seconds)) return std::nullopt;
    const auto& instance = instances[id];
    if (instance.removed || instance.severedJoint >= 0 || instance.asset >= dismembermentAssets.size() ||
        instance.asset >= assets.size()) return std::nullopt;
    const auto& joints = dismembermentAssets[instance.asset].joints;
    if (joints.empty()) return std::nullopt;
    const auto& asset = assets[instance.asset];
    std::vector<glm::mat4> pose;
    if (instance.hasPhysicalPose()) {
        if (instance.ragdollBones.size() != asset.skeleton.size()) return std::nullopt;
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            pose.push_back(instance.ragdollBones[bone] * glm::inverse(asset.skeleton[bone].inverseBind));
    } else pose = sampleZombiePose(asset, instance, seconds);
    std::optional<ZombieJointContact> selected;
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t j = 0; j < joints.size(); ++j) {
        float distance = 0;
        if (zombieJointSegment(start, end, instance.transform, joints[j], pose, distance) && distance < nearest) {
            nearest = distance;
            selected = ZombieJointContact{static_cast<int>(j), distance};
        }
    }
    return selected;
}

void ZombieLayer::severRagdollJoint(std::size_t id, int joint, glm::vec3 direction, ShotCaliber caliber) {
    auto& instance = instances[id];
    const auto part = dismembermentAssets.at(instance.asset).joints.at(std::size_t(joint)).part;
    const auto bodies = physics_->world.bodyTransforms(instance.ragdollHandle);
    const auto outward = glm::vec3(bodies[std::size_t(part)][3] - bodies[std::size_t(RagdollPart::pelvis)][3]);
    physics_->world.sever(instance.ragdollHandle, part);
    physics_->world.applyImpulse(instance.ragdollHandle, part, zombieSeverImpulse(caliber, direction, part, outward));
    physics_->world.addAngularVelocity(instance.ragdollHandle, part, zombieSeverSpin());
    instance.severedJoint = joint;
}

bool ZombieLayer::severJoint(std::size_t id, int joint, glm::vec3 point, glm::vec3 direction,
                            double seconds, const CollisionWorld& world) {
    const auto finite = [](glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const float length = glm::length(direction);
    if (id >= instances.size() || joint < 0 || !finite(point) || !finite(direction) ||
        !std::isfinite(seconds) || !std::isfinite(length) || length < 1e-6f) return false;
    auto& instance = instances[id];
    if (instance.removed || instance.severedJoint >= 0 || instance.asset >= assets.size() ||
        instance.asset >= dismembermentAssets.size() || std::size_t(joint) >= dismembermentAssets[instance.asset].joints.size())
        return false;
    direction /= length;
    if (instance.ragdoll) {
        if (!physics_ || !instance.physicsRagdoll) return false;
        severRagdollJoint(id, joint, direction, ShotCaliber::high);
        // The sever and impulses advance the physics revision. Publish the new
        // cut on the existing corpse immediately, even before the next solve.
        synchronizePhysics(id);
    } else activateRagdoll(id, sampleZombiePose(assets[instance.asset], instance, seconds), direction * 4.f,
                          seconds, joint, ShotCaliber::high);
    bloodMist_.emit(point, BloodMistSize::medium, seconds);
    emitBloodParticles({id, 0, false, joint, std::nullopt}, point, direction, seconds, &world);
    return true;
}
} // namespace viewer
