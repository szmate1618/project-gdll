#pragma once

#include "physics_prop.hpp"
#include "nail_interaction.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <memory>

namespace viewer {
class Camera;
class GroundItems;

// Authoritative nail previews and driven connections in the shared physics world.
// GroundItems owns the nail bodies and must outlive this controller. Replicas
// consume ground-item poses and do not create placement joints.
class NailPlacement final : public NailInteraction {
public:
    NailPlacement(GroundItems& items, std::shared_ptr<PhysicsWorld> world);
    ~NailPlacement() override;
    NailPlacement(const NailPlacement&) = delete;
    NailPlacement& operator=(const NailPlacement&) = delete;

    bool isNail(const PhysicsProp& nail) const override;
    bool snap(const PhysicsProp& nail, const Camera& rayCamera) override;
    bool pinned(const PhysicsProp& nail) const override;
    bool driven(const PhysicsProp& nail) const;
    glm::vec3 tip(const PhysicsProp& nail) const override;
    void rotate(const PhysicsProp& nail, glm::quat worldRotation) override;
    bool drive(const PhysicsProp& nail) override;
    void detach(const PhysicsProp& nail) override;
    std::size_t jointCount(const PhysicsProp& nail) const;

    // The authored 10 cm nail points along model-local -Y.
    static glm::vec3 localTip() { return {0, -.05f, 0}; }
    static constexpr float driveDistance = .099f;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
