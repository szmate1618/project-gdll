#pragma once

#include "model.hpp"
#include <memory>

namespace viewer {
class BoxLayer;
class DoorLayer;
class CollisionWorld;
class PhysicsScene;

// Immutable scene and doors are borrowed. Buildings are populated once per
// session, on the first successful grab of any of their doors.
class InteriorObjects {
public:
    InteriorObjects(const Model& model, const DoorLayer& doors, std::uint32_t seed);
    ~InteriorObjects();
    InteriorObjects(const InteriorObjects&) = delete;
    InteriorObjects& operator=(const InteriorObjects&) = delete;
    std::size_t populate(std::size_t door, const CollisionWorld& collision,
                         PhysicsScene& scene, BoxLayer& boxes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
