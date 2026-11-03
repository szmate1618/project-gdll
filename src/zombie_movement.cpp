#include "zombie_update_profile.hpp"
#include "zombie_movement.hpp"
#include "collision_world.hpp"
#include "zombie_crowd.hpp"
#include "character_step.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace viewer {
namespace {
constexpr float radius = ZombieCrowd::bodyRadius, height = ZombieCrowd::bodyHeight;
constexpr float skin = .002f, tolerance = .0001f;
constexpr float wallClearance = .05f; // Extra visual room for animated limbs, in meters.
constexpr float maxSegment = radius / 3;
constexpr int maxSegments = 2048, resolutionIterations = 4;
constexpr float stepHeight = .30f, minNormalY = .70710678f;

float horizontalDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

bool blocking(const Contact& contact, glm::vec3 feet) {
    if (contact.depth <= tolerance) return false;
    // Feet follow rendered terrain, not the elevated bottom of a capsule on a
    // slope. Supporting floors are allowed; low rails/props are still obstacles.
    if (contact.terrainSupport && contact.normal.y >= minNormalY) return false;
    if (contact.normal.y > .7f && contact.point.y <= feet.y + skin + tolerance) return false;
    return true;
}

auto key(const Contact& c) {
    return std::make_tuple(c.depth, c.normal.x, c.normal.y, c.normal.z,
                           c.point.x, c.point.y, c.point.z, c.terrain);
}

std::optional<Contact> blockingContact(glm::vec3 feet,const CollisionWorld& world,float padding,bool any,ContactCandidates* cache) {
    struct Selection {
        glm::vec3 feet;
        bool any;
        std::optional<Contact> deepest;
    } selection{feet,any,std::nullopt};
    world.visitContacts(feet,radius,height,padding,wallClearance,{&selection,[](void* context,const Contact& contact) {
        auto& selected=*static_cast<Selection*>(context);
        if (!blocking(contact,selected.feet)) return true;
        if (!selected.deepest || key(contact)>key(*selected.deepest)) selected.deepest=contact;
        return !selected.any;
    }},cache,ContactPolicy::walking);
    return selection.deepest;
}

bool clear(glm::vec3 feet,const CollisionWorld& world,float padding=0,ContactCandidates* cache=nullptr) {
    return !blockingContact(feet,world,padding,true,cache);
}

bool support(glm::vec3& feet, const CollisionWorld& world, float ceiling, WalkingGroundCache* cache,ContactCandidates* contacts) {
    const auto hit = world.walkingGroundAt(feet.x, feet.z,
        std::numeric_limits<float>::lowest(), ceiling, cache);
    if (!hit || hit->normal.y < minNormalY) return false;
    auto supported = feet;
    supported.y = hit->height;
    if (!hit->terrain) supported.y += radius * (1 / hit->normal.y - 1) + skin / hit->normal.y;
    // Keep rounded-foot edge support until the entire capsule can descend.
    // Dropping to the ground under its center too early reenters the ledge.
    const bool descending=supported.y<feet.y;
    if (descending && !clear(supported,world,skin,contacts)) return false;
    feet=supported;
    // The identical padded contact query has already proved these feet clear.
    // No world/prop/crowd mutation occurs before resolve consumes this result.
    return descending;
}

glm::vec3 resolve(glm::vec3 start, glm::vec3 desired, const CollisionWorld& world, WalkingGroundCache* cache,ContactCandidates* contacts) {
    auto feet = desired;
    const float allowance = horizontalDistance(start, desired);
    for (int iteration = 0; iteration < resolutionIterations; ++iteration) {
        if (support(feet,world,start.y+stepHeight,cache,contacts)) return feet;
        const auto deepest=blockingContact(feet,world,skin,false,contacts);
        if (!deepest) return feet;
        const glm::vec2 normal(deepest->normal.x, deepest->normal.z);
        const float squared = glm::dot(normal, normal);
        if (squared < 1e-6f) return start; // Ceiling or top of a low obstacle; no climbing.
        const auto correction = normal * (deepest->depth / squared);
        feet.x += correction.x;
        feet.z += correction.y;
        const float correctedTravel = horizontalDistance(start, feet);
        if (correctedTravel > allowance + tolerance) {
            // A glancing jamb correction can slightly exceed the requested
            // length. Shorten it and recheck contacts instead of discarding
            // every subsequent step at the same otherwise passable corner.
            const float scale = allowance / correctedTravel;
            feet.x = start.x + (feet.x - start.x) * scale;
            feet.z = start.z + (feet.z - start.z) * scale;
        }
    }
    support(feet, world, start.y + stepHeight, cache,contacts);
    return clear(feet, world,0,contacts) ? feet : start;
}
}

bool zombieHasPassingClearance(glm::vec3 feet, const CollisionWorld& world) {
    if (world.empty()) return true;
    struct Space { glm::vec3 feet; bool clear = true; } space{feet};
    // Leave two steering neighborhoods clear before spreading; otherwise the
    // back of a queue can fan out while its front is already entering a door.
    world.visitContacts(feet, radius, height, 3.f, wallClearance,
        {&space, [](void* context, const Contact& contact) {
            auto& space = *static_cast<Space*>(context);
            // With wide padding, a nearby floor triangle's edge can report a
            // sideways normal even though its surface is beneath the feet.
            if (contact.terrainSupport || contact.point.y <= space.feet.y + tolerance) return true;
            // Padding also reaches above/below the body. Only nearby sides
            // within its standing height constrain horizontal passing space.
            if (contact.depth > tolerance && std::abs(contact.normal.y) < minNormalY &&
                contact.point.y >= space.feet.y && contact.point.y <= space.feet.y + height) {
                space.clear = false;
                return false;
            }
            return true;
        }});
    return space.clear;
}

ZombieMovement moveZombieBody(glm::vec3 start, glm::vec3 desired, const CollisionWorld& world,
                              ZombieCrowd* crowd, std::size_t id, WalkingGroundCache* groundCache,
                              ContactCandidates* contactCache, std::size_t* sharedCrowdChecks) {
    zombie_profile::Timer timer(zombie_profile::Stage::bodyMovement);
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(start[axis]) || !std::isfinite(desired[axis])) return {start};
    std::size_t localCrowdChecks = 0;
    auto& crowdChecks = sharedCrowdChecks ? *sharedCrowdChecks : localCrowdChecks;
    if (world.empty()) {
        const auto feet = crowd ? crowd->constrainFrom(id, start, desired, crowdChecks) : desired;
        return {feet, horizontalDistance(start, feet)};
    }
    const glm::vec2 offset(desired.x - start.x, desired.z - start.z);
    const float length = glm::length(offset);
    ZombieMovement result{start};
    if (!std::isfinite(length) || length < 1e-5f) return result;
    const auto direction = offset / length;
    // Huge external timesteps stop after the budget; ordinary app steps need
    // only one or two segments. Never enlarge segments to consume excess time.
    float remaining = std::min(length, maxSegments * maxSegment);
    for (int segment = 0; segment < maxSegments && remaining > 1e-5f; ++segment) {
        const float step = std::min(remaining, maxSegment);
        auto proposed = result.feet + glm::vec3(direction.x * step, 0, direction.y * step);
        auto accepted = resolve(result.feet, proposed, world, groundCache,contactCache);
        if (horizontalDistance(accepted, proposed) > tolerance) {
            const CharacterStepConfig config{radius, height, stepHeight, minNormalY, skin, wallClearance};
            if (const auto stepped = tryCharacterStep(result.feet, proposed, world, config)) {
                const auto progress = [&](glm::vec3 feet) {
                    return glm::dot(glm::vec2(feet.x - result.feet.x, feet.z - result.feet.z), direction);
                };
                if (progress(stepped->feet) > progress(accepted) + 1e-6f) accepted = stepped->feet;
            }
        }
        if (crowd) {
            const auto sceneAccepted = accepted;
            accepted = crowd->constrainFrom(id, result.feet, accepted, crowdChecks);
            if (accepted.x != sceneAccepted.x || accepted.z != sceneAccepted.z) {
                if (accepted.x == result.feet.x && accepted.z == result.feet.z) accepted.y = result.feet.y;
                else support(accepted, world, result.feet.y + stepHeight, groundCache,contactCache);
                // A crowd slide can change the scene-resolved direction. Reject
                // it if it enters a wall; another correction could reenter the
                // crowd. Unchanged candidates already passed the scene check.
                if (!clear(accepted, world,0,contactCache)) break;
            }
        }
        const float travel = horizontalDistance(result.feet, accepted);
        if (travel < 1e-5f) break;
        result.feet = accepted;
        result.travel += travel;
        remaining -= step;
    }
    return result;
}
} // namespace viewer
