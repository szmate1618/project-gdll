#pragma once

#include <cstddef>
#include <memory>
#include <array>
#include <cstdint>
#include <box3d/id.h>

#include <glm/glm.hpp>

#include "zombie_skeleton.hpp"

namespace viewer {

class PhysicsWorld;

// World-space skeleton transforms and distal joint positions, in meters.
// Bone matrices may include asset scale; physics uses their rigid motion only.
// Torso's tip is the neck, head's tip is above the skull, and limb tips are
// elbows/hands/knees/feet. The pelvis tip is unused (hips define its width).
struct RagdollPose {
    std::array<glm::mat4, ragdollPartCount> bones{};
    std::array<glm::vec3, ragdollPartCount> tips{};
};

// Box3D-backed corpse and living attack rigs. Loose corpses retain their small
// local floor. The scene additionally supplies contacts for driven attack rigs.
class ZombieRagdollWorld {
public:
    ZombieRagdollWorld();
    explicit ZombieRagdollWorld(std::shared_ptr<PhysicsWorld> world);
    ~ZombieRagdollWorld();
    ZombieRagdollWorld(ZombieRagdollWorld&&) noexcept;
    ZombieRagdollWorld& operator=(ZombieRagdollWorld&&) noexcept;
    ZombieRagdollWorld(const ZombieRagdollWorld&) = delete;
    ZombieRagdollWorld& operator=(const ZombieRagdollWorld&) = delete;

    std::size_t create(glm::vec3 feet, float yaw, glm::vec3 impulse);
    std::size_t create(glm::vec3 feet, float yaw, glm::vec3 impulse,
                       const RagdollPose& referencePose, const RagdollPose& currentPose);
    void step(float seconds);
    bool valid(std::size_t handle) const;
    std::uint64_t poseRevision(std::size_t handle) const;
    bool sleeping(std::size_t handle) const;
    // True if any body's entire collider is below cutoffY (world Y-up meters),
    // including severed parts. The local fallback floor is excluded.
    bool hasBodyBelow(std::size_t handle, float cutoffY) const;
    // External mutations go through this owner so cached skinning cannot become
    // stale. Translation preserves the handoff reference; removal keeps IDs stable.
    void translate(std::size_t handle, glm::vec3 displacement);
    void applyImpulse(std::size_t handle, RagdollPart part, glm::vec3 impulse);
    // Add world-space linear velocity (meters/second) to this body only.
    void addLinearVelocity(std::size_t handle, RagdollPart part, glm::vec3 velocity);
    // Add world-space angular velocity (radians/second) to this body only.
    void addAngularVelocity(std::size_t handle, RagdollPart part, glm::vec3 velocity);
    void sever(std::size_t handle, RagdollPart child);
    // Capture every joint's current relative rotation as a damped spring target.
    // Anchor both feet at their current poses; the upper body remains dynamic.
    // This rig has no separate feet, so anchors lock the lower-leg frames.
    void holdPose(std::size_t handle);
    // Moving animation targets in world meters. Lower-body frames are supported;
    // torso/head/arms remain dynamic and follow damped joint springs.
    void drivePose(std::size_t handle, const RagdollPose& target, float seconds);
    void releasePose(std::size_t handle); // Preserve current transforms/velocities for death.
    bool ownsBody(std::size_t handle, b3BodyId body) const;
    void destroy(std::size_t handle);
    [[nodiscard]] glm::mat4 rootTransform(std::size_t handle) const;
    [[nodiscard]] std::array<glm::mat4, ragdollPartCount> bodyTransforms(std::size_t handle) const;
    // Body transforms at activation, for applying subsequent physics deltas
    // to the exact animation pose captured by the caller.
    [[nodiscard]] std::array<glm::mat4, ragdollPartCount> restBodyTransforms(std::size_t handle) const;
    [[nodiscard]] std::size_t count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
