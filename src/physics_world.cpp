#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
bool allowShapeContact(b3ShapeId shapeA, b3ShapeId shapeB, void*) {
    // Filters are fixed before stepping. Negative groups identify a ragdoll
    // and suppress self-contact; its floor carries the corresponding positive
    // group so the usual group rule does not reject its owner's contacts.
    const auto a = b3Shape_GetFilter(shapeA);
    const auto b = b3Shape_GetFilter(shapeB);
    if (a.categoryBits == zombieCavityCategory)
        return a.groupIndex < 0 && b.groupIndex == -a.groupIndex && b.categoryBits == zombieContentsCategory;
    if (b.categoryBits == zombieCavityCategory)
        return b.groupIndex < 0 && a.groupIndex == -b.groupIndex && a.categoryBits == zombieContentsCategory;
    if ((a.categoryBits & ragdollFloorCategory) != 0)
        return a.groupIndex > 0 && b.groupIndex == -a.groupIndex;
    if ((b.categoryBits & ragdollFloorCategory) != 0)
        return b.groupIndex > 0 && a.groupIndex == -b.groupIndex;
    return true;
}
} // namespace

PhysicsWorld::PhysicsWorld() {
    auto definition = b3DefaultWorldDef();
    definition.gravity = {0, -9.8f, 0};
    definition.enableSleep = true;
    definition.enableContinuous = true;
    world_ = b3CreateWorld(&definition);
    if (!b3World_IsValid(world_)) throw std::runtime_error("Unable to create Box3D world");
    b3World_SetCustomFilterCallback(world_, allowShapeContact, nullptr);
}
PhysicsWorld::~PhysicsWorld() { b3DestroyWorld(world_); }
void PhysicsWorld::step(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    constexpr float fixedStep = 1.0f / 60.0f;
    accumulator_ = std::min(accumulator_ + seconds, 0.2f);
    int steps = 0;
    while (accumulator_ >= fixedStep && steps++ < 8) {
        b3World_Step(world_, fixedStep, 4);
        const auto events = b3World_GetBodyEvents(world_);
        for (int i = 0; i < events.moveCount; ++i) {
            const auto found = motionOwners_.find(b3StoreBodyId(events.moveEvents[i].bodyId));
            if (found != motionOwners_.end()) ++*found->second;
        }
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
