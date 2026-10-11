#pragma once

#include <box3d/id.h>

namespace viewer {

// Shared Box3D simulation in runtime meters (X east, Y up, Z south).
// Owners of bodies/joints must retain this world and destroy their resources
// before releasing it. Advance once per application frame, then read all poses.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    void step(float seconds);
    b3WorldId id() const { return world_; }
    int allocateCollisionGroup(); // Negative groups suppress ragdoll self-contact.
private:
    b3WorldId world_{};
    float accumulator_ = 0;
    int nextCollisionGroup_ = -1;
};

} // namespace viewer
