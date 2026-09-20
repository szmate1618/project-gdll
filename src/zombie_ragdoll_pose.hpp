#pragma once

namespace viewer {

struct AnimatedModel;
struct ZombieInstance;
struct RagdollPose;
class ZombieRagdollWorld;

// Convert a character's reference and captured poses to physical bone/endpoints
// in world meters. False means the asset lacks the required humanoid bones.
bool makeZombieRagdollPoses(const ZombieInstance& instance, const AnimatedModel& asset,
                           RagdollPose& reference, RagdollPose& current);

// Apply simulated body motion to the captured pose, retaining unmapped child
// joints (hands, fingers, feet, etc.) in their captured parent-relative poses.
void updateZombieRagdollSkin(ZombieInstance& instance, const AnimatedModel& asset,
                            const ZombieRagdollWorld& world);

} // namespace viewer
