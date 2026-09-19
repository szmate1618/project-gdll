#pragma once

#include <cstddef>
#include <memory>

#include <glm/glm.hpp>

namespace viewer {

// Box3D-backed ragdoll simulation. The viewer's animated meshes are baked into
// vertex textures, so this deliberately exposes the pelvis/root pose instead of
// requiring a second live skinning path.
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
    [[nodiscard]] std::size_t count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer
