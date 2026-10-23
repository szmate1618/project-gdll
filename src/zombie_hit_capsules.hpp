#pragma once

#include <glm/glm.hpp>
#include <filesystem>
#include <vector>

namespace viewer {
struct AnimatedModel;

struct ZombieBoneCapsule {
    int bone = -1;
    glm::vec3 a{0}, b{0}; // Segment endpoints in reference-bone meters.
    float radius = 0;
    bool headshot = false;
    glm::mat4 boneFromMeters{1}; // Reference-meter frame to source bone units.
};

struct ZombieCapsuleAsset {
    std::vector<ZombieBoneCapsule> capsules;
    std::vector<std::filesystem::path> resources;
};

// Optional BASE.capsules.json, fitted offline. Present but stale/invalid files
// fail loading; absent definitions retain the custom-rig shooting fallback.
ZombieCapsuleAsset loadZombieHitCapsules(const std::filesystem::path& basePath, const AnimatedModel& asset);

// The unnormalized local ray preserves its world-space parameter through
// bone animation, instance scaling/reflection and shear. Inside hits return 0.
bool zombieCapsuleRay(glm::vec3 origin, glm::vec3 direction, const glm::mat4& instance,
                      const ZombieBoneCapsule& capsule, const std::vector<glm::mat4>& pose, float& distance);
} // namespace viewer
