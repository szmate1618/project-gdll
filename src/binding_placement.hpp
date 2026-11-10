#pragma once

#include "binding_interaction.hpp"
#include <glm/vec3.hpp>
#include <cstddef>
#include <memory>
#include <vector>

namespace viewer {
class GroundItems;
class PhysicsWorld;

// Temporary bounded-force pulls use ordinary collision response. Releasing
// replaces them with rope limits at the reached separation, without teleporting.
class BindingPlacement final : public BindingInteraction {
public:
    BindingPlacement(GroundItems&, std::shared_ptr<PhysicsWorld>);
    ~BindingPlacement() override;
    bool isBinding(const PhysicsProp&) const override;
    void update(const PhysicsProp&, const Camera&) override;
    bool finish(const PhysicsProp&) override;
    void cancel(const PhysicsProp&) override;
    const std::vector<glm::vec3>& lines() const;
    std::size_t jointCount() const; // Completed ties only.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
