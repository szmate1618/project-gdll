#include "zombie_movement.hpp"
#include "collision_world.hpp"
#include "zombie_crowd.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace viewer {
namespace {
constexpr float radius = ZombieCrowd::bodyRadius, height = ZombieCrowd::bodyHeight;
constexpr float skin = .002f, tolerance = .0001f;
constexpr float maxSegment = radius / 3;
constexpr int maxSegments = 2048, resolutionIterations = 4;

float horizontalDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

void support(glm::vec3& feet, const CollisionWorld& world) {
    const bool terrainOnly = world.stats().terrainTriangles != 0;
    const auto hit = world.groundAt(feet.x, feet.z,
        std::numeric_limits<float>::lowest(),
        terrainOnly ? std::numeric_limits<float>::max() : feet.y + skin, terrainOnly);
    if (hit) feet.y = hit->height;
}

bool blocking(const Contact& contact, glm::vec3 feet) {
    if (contact.depth <= tolerance) return false;
    // Feet follow rendered terrain, not the elevated bottom of a capsule on a
    // slope. Supporting floors are allowed; low rails/props are still obstacles.
    if (contact.terrain && contact.normal.y > .5f) return false;
    if (contact.normal.y > .7f && contact.point.y <= feet.y + skin + tolerance) return false;
    return true;
}

auto key(const Contact& c) {
    return std::make_tuple(c.depth, c.normal.x, c.normal.y, c.normal.z,
                           c.point.x, c.point.y, c.point.z, c.terrain);
}

bool clear(glm::vec3 feet, const CollisionWorld& world) {
    for (const auto& c : world.contacts(feet, radius, height))
        if (blocking(c, feet)) return false;
    return true;
}

glm::vec3 resolve(glm::vec3 start, glm::vec3 desired, const CollisionWorld& world) {
    auto feet = desired;
    const float allowance = horizontalDistance(start, desired);
    for (int iteration = 0; iteration < resolutionIterations; ++iteration) {
        support(feet, world);
        const auto contacts = world.contacts(feet, radius, height, skin);
        const Contact* deepest = nullptr;
        for (const auto& contact : contacts)
            if (blocking(contact, feet) && (!deepest || key(contact) > key(*deepest))) deepest = &contact;
        if (!deepest) return feet;
        const glm::vec2 normal(deepest->normal.x, deepest->normal.z);
        const float squared = glm::dot(normal, normal);
        if (squared < 1e-6f) return start; // Ceiling or top of a low obstacle; no climbing.
        const auto correction = normal * (deepest->depth / squared);
        feet.x += correction.x;
        feet.z += correction.y;
        if (horizontalDistance(start, feet) > allowance + tolerance) return start;
    }
    support(feet, world);
    return clear(feet, world) ? feet : start;
}
}

ZombieMovement moveZombieBody(glm::vec3 start, glm::vec3 desired, const CollisionWorld& world,
                              ZombieCrowd* crowd, std::size_t id) {
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(start[axis]) || !std::isfinite(desired[axis])) return {start};
    if (world.empty()) {
        const auto feet = crowd ? crowd->constrain(id, desired) : desired;
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
    std::size_t crowdChecks = 0;
    for (int segment = 0; segment < maxSegments && remaining > 1e-5f; ++segment) {
        const float step = std::min(remaining, maxSegment);
        auto proposed = result.feet + glm::vec3(direction.x * step, 0, direction.y * step);
        auto accepted = resolve(result.feet, proposed, world);
        if (crowd) {
            const auto sceneAccepted = accepted;
            accepted = crowd->constrainFrom(id, result.feet, accepted, crowdChecks);
            if (accepted.x != sceneAccepted.x || accepted.z != sceneAccepted.z) {
                if (accepted.x == result.feet.x && accepted.z == result.feet.z) accepted.y = result.feet.y;
                else support(accepted, world);
                // A crowd slide can change the scene-resolved direction. Reject
                // it if it enters a wall; another correction could reenter the
                // crowd. Unchanged candidates already passed the scene check.
                if (!clear(accepted, world)) break;
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
