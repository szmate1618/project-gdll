#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace viewer {

struct AnimatedModel;
struct ZombieInstance;
struct RagdollPose;
class ZombieRagdollWorld;

// Convert a character's reference and captured poses to physical bone/endpoints
// in world meters. False means the asset lacks the required humanoid bones.
bool makeZombieRagdollPoses(const ZombieInstance& instance, const AnimatedModel& asset,
                           RagdollPose& reference, RagdollPose& current);

// Estimate each mapped body's center velocity from the captured pose and an
// earlier pose of the same clip. Adds world-space m/s; limits clip discontinuities.
void inheritZombieRagdollMotion(const ZombieInstance& instance, const AnimatedModel& asset,
                               const std::vector<glm::mat4>& previousPose, float seconds,
                               ZombieRagdollWorld& world);

// Apply simulated body motion to the captured pose, retaining unmapped child
// joints (hands, fingers, feet, etc.) in their captured parent-relative poses.
void updateZombieRagdollSkin(ZombieInstance& instance, const AnimatedModel& asset,
                            const ZombieRagdollWorld& world);

} // namespace viewer
