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
void PhysicsWorld::step(float seconds, const std::function<void(float)>& before,
                        const std::function<void(float)>& after) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    constexpr float fixedStep = 1.0f / 60.0f;
    accumulator_ = std::min(accumulator_ + seconds, 0.2f);
    int steps = 0;
    while (accumulator_ >= fixedStep && steps++ < 8) {
        if (before) before(fixedStep);
        b3World_Step(world_, fixedStep, 4);
        const auto events = b3World_GetBodyEvents(world_);
        for (int i = 0; i < events.moveCount; ++i) {
            const auto found = motionOwners_.find(b3StoreBodyId(events.moveEvents[i].bodyId));
            if (found != motionOwners_.end()) ++*found->second;
        }
        if (after) after(fixedStep);
        accumulator_ -= fixedStep;
    }
}
void PhysicsWorld::trackBodyMotion(b3BodyId body, std::uint64_t& ownerRevision) {
    if (!b3Body_IsValid(body)) throw std::invalid_argument("Cannot track an invalid physics body");
    const auto result = motionOwners_.emplace(b3StoreBodyId(body), &ownerRevision);
    if (!result.second) throw std::logic_error("Physics body already has a motion owner");
}
void PhysicsWorld::untrackBodyMotion(b3BodyId body) {
    motionOwners_.erase(b3StoreBodyId(body));
}
int PhysicsWorld::allocateCollisionGroup() {
    if (nextCollisionGroup_ == std::numeric_limits<int>::min())
        throw std::runtime_error("Physics collision groups exhausted");
    return nextCollisionGroup_--;
}
} // namespace viewer
