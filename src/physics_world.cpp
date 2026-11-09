#include "physics_world.hpp"
#include "frame_stage_profile.hpp"
#include "physics_impacts.hpp"
#include "corpse_settling.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

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

PhysicsWorld::PhysicsWorld(const std::filesystem::path& acousticStorage) {
    auto definition = b3DefaultWorldDef();
    definition.gravity = {0, -9.8f, 0};
    definition.enableSleep = true;
    definition.enableContinuous = true;
    world_ = b3CreateWorld(&definition);
    if (!b3World_IsValid(world_)) throw std::runtime_error("Unable to create Box3D world");
    b3World_SetCustomFilterCallback(world_, allowShapeContact, nullptr);
    try {
        impacts_ = std::make_unique<PhysicsImpacts>(world_, acousticStorage);
        corpseSettling_ = std::make_unique<CorpseSettling>(*this);
    }
    catch (...) { b3DestroyWorld(world_); throw; }
}
PhysicsWorld::~PhysicsWorld() { b3DestroyWorld(world_); }
void PhysicsWorld::step(float seconds, const std::function<void(float)>& before,
                        const std::function<void(float)>& after) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    accumulator_ = std::min(accumulator_ + seconds, 0.2f);
    int steps = 0;
    while (accumulator_ >= fixedStepSeconds && steps++ < 8) {
        if (before) before(fixedStepSeconds);
        for (const auto& callbacks : stepCallbacks_) callbacks.second.prepare();
        corpseSettling_->prepare();
        {
            frame_stage_profile::Timer timer(frame_stage_profile::Stage::physics);
            b3World_Step(world_, fixedStepSeconds, 4);
        }
        corpseSettling_->finish();
        const auto events = b3World_GetBodyEvents(world_);
        for (int i = 0; i < events.moveCount; ++i) {
            const auto found = motionOwners_.find(b3StoreBodyId(events.moveEvents[i].bodyId));
            if (found != motionOwners_.end()) ++*found->second;
        }
        impacts_->capture();
        for (const auto& callbacks : stepCallbacks_) callbacks.second.finish();
        if (after) after(fixedStepSeconds);
        accumulator_ -= fixedStepSeconds;
    }
}
float PhysicsWorld::interpolationAlpha() const {
    return std::clamp(accumulator_ / fixedStepSeconds, 0.f, 1.f);
}
void PhysicsWorld::trackBodyMotion(b3BodyId body, std::uint64_t& ownerRevision) {
    if (!b3Body_IsValid(body)) throw std::invalid_argument("Cannot track an invalid physics body");
    const auto result = motionOwners_.emplace(b3StoreBodyId(body), &ownerRevision);
    if (!result.second) throw std::logic_error("Physics body already has a motion owner");
}
void PhysicsWorld::untrackBodyMotion(b3BodyId body) {
    motionOwners_.erase(b3StoreBodyId(body));
}
void PhysicsWorld::registerStepCallbacks(const void* owner, std::function<void()> prepare, std::function<void()> finish) {
    if (!owner || !prepare || !finish) throw std::invalid_argument("Physics step callbacks require an owner and two callbacks");
    if (!stepCallbacks_.emplace(owner, StepCallbacks{std::move(prepare), std::move(finish)}).second)
        throw std::logic_error("Physics step callbacks already registered");
}
void PhysicsWorld::unregisterStepCallbacks(const void* owner) {
    stepCallbacks_.erase(owner);
}
bool PhysicsWorld::isGrabbed(b3BodyId body) const { return grabCounts_.count(b3StoreBodyId(body)) != 0; }
void PhysicsWorld::beginGrab(b3BodyId body) {
    for (const auto& callback : grabCallbacks_) callback.second(body);
    ++grabCounts_[b3StoreBodyId(body)];
    corpseSettling_->invalidate(body);
}
void PhysicsWorld::registerGrabCallback(const void* owner, std::function<void(b3BodyId)> callback) {
    if (!owner || !callback || !grabCallbacks_.emplace(owner, std::move(callback)).second)
        throw std::logic_error("Invalid or duplicate grab callback");
}
void PhysicsWorld::unregisterGrabCallback(const void* owner) { grabCallbacks_.erase(owner); }
void PhysicsWorld::endGrab(b3BodyId body) {
    const auto found = grabCounts_.find(b3StoreBodyId(body));
    if (found != grabCounts_.end() && --found->second == 0) grabCounts_.erase(found);
    corpseSettling_->invalidate(body);
}
int PhysicsWorld::allocateCollisionGroup() {
    if (nextCollisionGroup_ == std::numeric_limits<int>::min())
        throw std::runtime_error("Physics collision groups exhausted");
    return nextCollisionGroup_--;
}
} // namespace viewer
