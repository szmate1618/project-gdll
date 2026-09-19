#pragma once

#include <cstddef>
#include <memory>
#include <array>

#include <glm/glm.hpp>

#include "zombie_skeleton.hpp"

namespace viewer {

// Box3D-backed ragdoll simulation. Ground collision is intentionally represented
// by a small plane-sized box around each activated zombie.
class ZombieRagdollWorld {
public:
    ZombieRagdollWorld();
    ~ZombieRagdollWorld();
    ZombieRagdollWorld(ZombieRagdollWorld&&) noexcept;
    ZombieRagdollWorld& operator=(ZombieRagdollWorld&&) noexcept;
    ZombieRagdollWorld(const ZombieRagdollWorld&) = delete;
    ZombieRagdollWorld& operator=(const ZombieRagdollWorld&) = delete;

    std::size_t create(glm::vec3 feet, float yaw, glm::vec3 impulse);
    void step(float seconds);
    [[nodiscard]] glm::mat4 rootTransform(std::size_t handle) const;
    [[nodiscard]] std::array<glm::mat4, ragdollPartCount> bodyTransforms(std::size_t handle) const;
    [[nodiscard]] std::array<glm::mat4, ragdollPartCount> restBodyTransforms(std::size_t handle) const;
    [[nodiscard]] std::size_t count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
