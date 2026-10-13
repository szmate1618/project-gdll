#pragma once

#include <cstddef>
#include <memory>
#include <array>
#include <cstdint>

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

// Box3D-backed ragdoll simulation. Ground collision is intentionally represented
// by a small plane-sized box around each activated zombie.
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
    // External mutations go through this owner so cached skinning cannot become
    // stale. Translation preserves the handoff reference; removal keeps IDs stable.
    void translate(std::size_t handle, glm::vec3 displacement);
    void applyImpulse(std::size_t handle, RagdollPart part, glm::vec3 impulse);
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
