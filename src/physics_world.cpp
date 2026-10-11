#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
PhysicsWorld::PhysicsWorld() {
    auto definition = b3DefaultWorldDef();
    definition.gravity = {0, -9.8f, 0};
    definition.enableSleep = true;
    definition.enableContinuous = true;
    world_ = b3CreateWorld(&definition);
    if (!b3World_IsValid(world_)) throw std::runtime_error("Unable to create Box3D world");
}
PhysicsWorld::~PhysicsWorld() { b3DestroyWorld(world_); }
void PhysicsWorld::step(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    constexpr float fixedStep = 1.0f / 60.0f;
    accumulator_ = std::min(accumulator_ + seconds, 0.2f);
    int steps = 0;
    while (accumulator_ >= fixedStep && steps++ < 8) {
        b3World_Step(world_, fixedStep, 4);
        accumulator_ -= fixedStep;
    }
}
int PhysicsWorld::allocateCollisionGroup() {
    if (nextCollisionGroup_ == std::numeric_limits<int>::min())
        throw std::runtime_error("Physics collision groups exhausted");
    return nextCollisionGroup_--;
}
} // namespace viewer
