#include "dog_layer.hpp"
#include "character_step.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace viewer {
namespace {
constexpr float radius = dogRadius, height = dogHeight;
constexpr float skin = .002f, tolerance = .0001f, wallClearance = .05f;
constexpr float stepHeight = .30f, minNormalY = .70710678f;
constexpr int maxSegments = 2048, resolutionIterations = 4;
float horizontalDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(glm::vec2(a.x-b.x,a.z-b.z));
}
bool blocking(const Contact& contact, glm::vec3 feet) {
    if (contact.depth <= tolerance) return false;
    // Feet follow rendered terrain, not the elevated bottom of a capsule on a
    // slope. Supporting floors are allowed; low rails/props are still obstacles.
    if (contact.terrainSupport && contact.normal.y >= minNormalY) return false;
    if (contact.normal.y > .7f && contact.point.y <= feet.y + tolerance) return false;
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

enum class Support { unavailable, retained, lowered };
Support support(glm::vec3& feet, const CollisionWorld& world, float ceiling, WalkingGroundCache* cache,ContactCandidates* contacts) {
    const auto hit = world.walkingGroundAt(feet.x, feet.z,
        std::numeric_limits<float>::lowest(), ceiling, cache);
    if (!hit || hit->normal.y < minNormalY) return Support::unavailable;
    auto supported = feet;
    supported.y = hit->height;
    if (!hit->terrain) supported.y += radius * (1 / hit->normal.y - 1) + skin / hit->normal.y;
    // Keep rounded-foot edge support until the entire capsule can descend.
    // Dropping to the ground under its center too early reenters the ledge.
    const bool descending=supported.y<feet.y;
    if (descending && !clear(supported,world,skin,contacts)) return Support::retained;
    feet=supported;
    // The identical padded contact query has already proved these feet clear.
    // No world/prop/crowd mutation occurs before resolve consumes this result.
    return descending ? Support::lowered : Support::retained;
}

glm::vec3 resolve(glm::vec3 start, glm::vec3 desired, const CollisionWorld& world, WalkingGroundCache* cache,ContactCandidates* contacts) {
    auto feet = desired;
    const float allowance = horizontalDistance(start, desired);
    for (int iteration = 0; iteration < resolutionIterations; ++iteration) {
        const auto ground = support(feet,world,start.y+stepHeight,cache,contacts);
        if (ground == Support::unavailable) return start;
        if (ground == Support::lowered) return feet;
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
    if (support(feet,world,start.y+stepHeight,cache,contacts) == Support::unavailable) return start;
    return clear(feet, world,0,contacts) ? feet : start;
}
}

glm::vec3 DogLayer::move(std::size_t id, glm::vec3 desired, const CollisionWorld& world) {
    auto feet = glm::vec3(instances[id].root[3]);
    const auto offset = glm::vec2(desired.x-feet.x,desired.z-feet.z);
    const float distance = glm::length(offset);
    if (!std::isfinite(distance) || distance < 1e-6f) return feet;
    const auto direction = offset/distance;
    constexpr float maxSegment = radius/3;
    float remaining = std::min(distance,maxSegments*maxSegment);
    WalkingGroundCache groundCache;
    ContactCandidates contactCache;
    for (int segment = 0; segment < maxSegments && remaining > 1e-6f; ++segment) {
        const float length = std::min(remaining,maxSegment);
        const auto proposed = feet+glm::vec3(direction.x*length,0,direction.y*length);
        auto accepted = proposed;
        if (!world.empty()) {
            accepted = resolve(feet,proposed,world,&groundCache,&contactCache);
            if (horizontalDistance(accepted,proposed) > tolerance) {
                const CharacterStepConfig config{radius,height,stepHeight,minNormalY,skin,wallClearance};
                if (const auto step = tryCharacterStep(feet,proposed,world,config)) {
                    const auto progress = [&](glm::vec3 point) {
                        return glm::dot(glm::vec2(point.x-feet.x,point.z-feet.z),direction);
                    };
                    if (progress(step->feet) > progress(accepted)+1e-6f) accepted = step->feet;
                }
            }
        }
        spatial_.query(accepted,radius*2,candidates_);
        bool blocked = false;
        for (const auto other : candidates_) {
            if (other == id) continue;
            const auto position = glm::vec3(instances[other].root[3]);
            if (std::abs(position.y-accepted.y) >= height) continue;
            if (glm::length(glm::vec2(position.x-accepted.x,position.z-accepted.z)) < radius*2-tolerance) {
                blocked = true;
                break;
            }
        }
        if (blocked || horizontalDistance(feet,accepted) < 1e-6f) break;
        feet = accepted;
        remaining -= length;
    }
    return feet;
}
} // namespace viewer
