#include "dog_layer.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
bool passingClearance(glm::vec3 feet, const CollisionWorld& world) {
    if (world.empty()) return true;
    struct Space { glm::vec3 feet; bool clear = true; } space{feet};
    // Stop spreading before reaching a jamb or narrow corridor. Contact
    // padding checks dog-height sides, without additional sight observations.
    world.visitContacts(feet, dogRadius, dogHeight, 3.f, .05f,
        {&space, [](void* context, const Contact& contact) {
            auto& space = *static_cast<Space*>(context);
            if (contact.terrainSupport || contact.point.y <= space.feet.y + .0001f) return true;
            if (contact.depth > .0001f && std::abs(contact.normal.y) < .70710678f &&
                contact.point.y >= space.feet.y && contact.point.y <= space.feet.y + dogHeight) {
                space.clear = false;
                return false;
            }
            return true;
        }});
    return space.clear;
}
}

glm::vec2 DogLayer::steeringDirection(std::size_t id, glm::vec2 targetOffset, const CollisionWorld& world) {
    const float distance = glm::length(targetOffset);
    if (!std::isfinite(distance) || distance < 1e-4f) return {0, 0};
    const auto forward = targetOffset / distance;
    const float fade = std::clamp((distance - .15f) / .85f, 0.f, 1.f);
    if (fade == 0) return forward;
    constexpr float range = 1.5f, spacing = 1.f, laneWidth = dogRadius * 2 + .05f;
    constexpr std::size_t neighborLimit = 64;
    const auto feet = glm::vec3(instances[id].root[3]);
    spatial_.query(feet, range, candidates_);
    const glm::vec2 right(forward.y, -forward.x);
    const float passingSide = id % 2 == 0 ? -1.f : 1.f;
    float leftPressure = 0, rightPressure = 0, passLeft = 0, passRight = 0;
    std::size_t neighbors = 0;
    for (const auto other : candidates_) {
        if (other == id) continue;
        const auto& dog = instances[other];
        if (dog.dead || dog.removed || dog.asset >= assets.size()) continue;
        const auto position = glm::vec3(dog.root[3]);
        if (std::abs(position.y - feet.y) >= dogHeight) continue;
        const glm::vec2 offset(position.x - feet.x, position.z - feet.z);
        const float length = glm::length(offset);
        if (length >= range) continue;
        if (neighbors++ == neighborLimit) return forward;
        const float across = glm::dot(offset, right);
        const float pressure = std::max(0.f, 1 - length / spacing) * across / std::max(length, .01f);
        leftPressure = std::max(leftPressure, pressure);
        rightPressure = std::max(rightPressure, -pressure);
        const float along = glm::dot(offset, forward);
        if (along > 0 && std::abs(across) < laneWidth) {
            const float pressure = (1 - along / range) * (1 - std::abs(across) / laneWidth);
            const float side = std::abs(across) < .15f ? passingSide : (across > 0 ? -1.f : 1.f);
            if (side < 0) passLeft = std::max(passLeft, pressure);
            else passRight = std::max(passRight, pressure);
        }
    }
    const float sideways = fade * (.6f * (rightPressure - leftPressure) + 1.2f * (passRight - passLeft));
    if (sideways == 0 || !passingClearance(feet, world)) return forward;
    return glm::normalize(forward + right * sideways);
}
} // namespace viewer
