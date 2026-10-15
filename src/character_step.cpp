#include "character_step.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
constexpr float tolerance = .0001f;
bool blocked(glm::vec3 feet, const CollisionWorld& world, const CharacterStepConfig& config) {
    for (const auto& c : world.contacts(feet, config.radius, config.height, config.skin, config.wallClearance))
        if (c.depth > tolerance) return true;
    return false;
}
}

std::optional<CharacterStep> tryCharacterStep(glm::vec3 start, glm::vec3 desired,
                                             const CollisionWorld& world,
                                             const CharacterStepConfig& config) {
    const glm::vec2 offset(desired.x - start.x, desired.z - start.z);
    const float distance = glm::length(offset);
    if (!std::isfinite(distance) || distance < 1e-6f || distance > config.radius / 3 + tolerance ||
        config.maxRise <= 0 || !blocked(desired, world, config)) return std::nullopt;

    // The center is still outside a ledge when the rounded foot first touches
    // its edge. Looking only directly beneath the center cannot discover it.
    const auto forward = offset / distance;
    const auto ground = world.walkingGroundAt(start.x, start.z,
        start.y - config.radius - config.skin, start.y + config.skin);
    std::optional<GroundHit> support;
    for (const float reach : {0.f, config.radius * .5f, config.radius}) {
        const auto sample = glm::vec2(desired.x, desired.z) + forward * reach;
        // Measure the ledge above the supporting slope at its edge, rather
        // than above feet still a capsule radius downhill from that edge.
        float slopeRise = 0;
        float baseHeight = start.y;
        if (ground && ground->normal.y >= config.minNormalY) {
            const auto delta = sample - glm::vec2(start.x, start.z);
            slopeRise = -(ground->normal.x * delta.x + ground->normal.z * delta.y) / ground->normal.y;
            baseHeight = ground->height;
        }
        const auto hit = world.walkingGroundAt(sample.x, sample.y,
            start.y + tolerance, baseHeight + slopeRise + config.maxRise + tolerance);
        if (hit && hit->normal.y >= config.minNormalY &&
            (!support || hit->height > support->height)) support = hit;
    }
    if (!support) return std::nullopt;
    const float liftedY = support->height + config.radius * (1 / support->normal.y - 1) +
                          config.skin / support->normal.y;
    if (liftedY <= start.y + tolerance) return std::nullopt;

    const int lifts = std::max(1, static_cast<int>(std::ceil((liftedY - start.y) / (config.radius / 3))));
    for (int i = 1; i <= lifts; ++i) {
        auto lifted = start;
        lifted.y = start.y + (liftedY - start.y) * (float(i) / lifts);
        for (const auto& c : world.contacts(lifted, config.radius, config.height, config.skin, config.wallClearance)) {
            // Rising away from the supporting surface is allowed. A ceiling or
            // side contact blocks the lift, including thin overhead geometry.
            if (c.depth > tolerance && c.normal.y <= 0) return std::nullopt;
        }
    }
    auto landing = desired;
    landing.y = liftedY;
    if (blocked(landing, world, config)) return std::nullopt;

    // The downward range is shorter than the capsule height, so the first
    // overlap cannot be skipped. Bisection retains the clear side of contact,
    // including rounded-foot support just outside a raised surface's edge.
    float low = start.y, high = liftedY;
    auto bottom = landing;
    bottom.y = low;
    bool upwardContact = false;
    for (const auto& c : world.contacts(bottom, config.radius, config.height, config.skin, config.wallClearance))
        if (c.depth > tolerance && c.normal.y > 0) upwardContact = true;
    if (!upwardContact) return std::nullopt;
    for (int i = 0; i < 12; ++i) {
        landing.y = (low + high) * .5f;
        if (blocked(landing, world, config)) low = landing.y;
        else high = landing.y;
    }
    landing.y = high;
    return CharacterStep{landing, support->normal};
}
} // namespace viewer
