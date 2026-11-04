#pragma once

#include "animated_model.hpp"
#include <optional>

namespace viewer {

struct ZombieInstance;
class CollisionWorld;
class ZombieCrowd;
class WalkingGroundCache;
class ContactCandidates;
struct ZombieMovement;

// Explicit idle/attack/looking transitions stop immediately, bypassing the
// short animation hold used for intermittent locomotion obstruction.
void stopZombieLocomotion(ZombieInstance& instance);
// Rotate in place along the shortest yaw arc at 90 degrees/s. Returns true
// when aligned. Keep the idle animation and the asset's normalization intact.
bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds);

// Apply an already accepted physical movement to facing and locomotion clips.
// Shared by direct behavior movement and scenario routes; no navigation policy.
void applyZombieLocomotion(ZombieInstance& instance, glm::vec2 desiredVelocity,
                           const ZombieMovement& movement, float speed, float seconds);

// Direct pursuit in X/Z, constrained by scene, crowd and terrain. With a crowd,
// local spacing and a stable passing side bend the preferred direction without
// visibility queries; the final approach fades back to the direct heading.
// Nearby scene contacts suppress spreading to preserve narrow doorway passage.
// A nearby open doorway can supply temporary approach/exit/yield points. These
// never replace the original target; queue-point arrival is not target arrival.
// Returns true only on actual arrival; blocked characters retain their target.
// Small contact slides retain facing. Locomotion turns at most 180 degrees/s,
// with a short gait hold/debounce while collision progress is intermittent.
// Optional caller-owned caches retain only static candidates across moves.
// Optional output receives an actual door contact from this movement only.
bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& asset,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double animationSeconds,
                      ZombieCrowd* crowd = nullptr, std::size_t id = 0,
                      WalkingGroundCache* groundCache = nullptr, ContactCandidates* contactCache = nullptr,
                      std::optional<glm::vec3>* doorContact = nullptr);

} // namespace viewer
