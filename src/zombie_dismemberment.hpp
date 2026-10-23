#pragma once

#include "animated_model.hpp"
#include "weapon_caliber.hpp"
#include <filesystem>
#include <vector>

namespace viewer {

struct ZombieJointCollider {
    std::string name;
    int bone = -1;
    RagdollPart part = RagdollPart::pelvis;
    glm::vec3 center{0}; // Meters in the bone's reference orientation.
    float radius = 0;
    // Converts the meter-based collider frame to the source bone's local units.
    glm::mat4 boneFromMeters{1};
};

struct ZombieDismembermentAsset {
    std::vector<ZombieJointCollider> joints;
    // One shared capped mesh per joint, containing both sides of the cut.
    // Weights never cross that cut; the usual palette moves both assemblies.
    std::vector<AnimatedModel> meshes;
    std::vector<std::filesystem::path> resources;
};

// Optional BASE.hitboxes.json beside the original asset. Present but stale or
// malformed definitions fail loading rather than combining incompatible rigs.
ZombieDismembermentAsset loadZombieDismemberment(const std::filesystem::path& basePath,
                                                const AnimatedModel& base);
glm::mat4 zombieJointTransform(const ZombieJointCollider& joint, const std::vector<glm::mat4>& pose);
bool zombieJointRay(glm::vec3 origin, glm::vec3 direction, const glm::mat4& instance,
                    const ZombieJointCollider& joint, const std::vector<glm::mat4>& pose, float& distance);
// Shot impulse: low caliber 4..8 N s, high caliber 10..16 N s. Non-head
// parts also receive the same magnitude along their world-space center offset.
glm::vec3 zombieSeverImpulse(ShotCaliber caliber, glm::vec3 normalizedShotDirection,
                            RagdollPart part, glm::vec3 offsetFromZombieCenter);
// Random world-space spin axis, with angular speed 1..4 radians/second.
glm::vec3 zombieSeverSpin();

} // namespace viewer
