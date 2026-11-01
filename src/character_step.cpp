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
        config.maxRise <= 0) return std::nullopt;
    const auto contacts = world.contacts(desired, config.radius, config.height, config.skin, config.wallClearance);
    if (std::none_of(contacts.begin(), contacts.end(), [](const Contact& contact) {
            return contact.depth > tolerance;
        })) return std::nullopt;

    // The center is still outside a ledge when the rounded foot first touches
    // its edge. Looking only directly beneath the center cannot discover it.
    const auto forward = offset / distance;
    const auto ground = world.walkingGroundAt(start.x, start.z,
        start.y - config.radius - config.skin, start.y + config.skin);
    std::optional<GroundHit> support;
    const auto considerSupport = [&](glm::vec2 sample) {
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
    };
    for (const float reach : {0.f, config.radius * .5f, config.radius})
        considerSupport(glm::vec2(desired.x, desired.z) + forward * reach);
    // A descending tread can have its highest point at the lip, between all
    // three forward samples. Probe the actual contact to continue rolling the
    // rounded foot across it without losing the ledge's support.
    for (const auto& contact : contacts) {
        if (contact.depth <= tolerance) continue;
        const auto sample = glm::vec2(contact.point.x, contact.point.z);
        const auto delta = sample - glm::vec2(start.x, start.z);
        const float along = glm::dot(delta, forward);
        const auto across = delta - forward * along;
        if (along >= -tolerance && along <= distance + config.radius + tolerance &&
            glm::length(across) <= config.radius + tolerance)
            considerSupport(sample);
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

    // A step can exceed the foot radius (dogs use a 25 cm radius and 30 cm
    // rise). At the original height its side contact may have no upward normal;
    // find the first downward contact before bisecting the supported landing.
    float low = start.y, high = liftedY;
    bool upwardContact = false;
    for (int i = 1; i <= lifts; ++i) {
        auto probe = landing;
        probe.y = liftedY - (liftedY-start.y)*(float(i)/lifts);
        bool overlap = false;
        for (const auto& c : world.contacts(probe, config.radius, config.height, config.skin, config.wallClearance)) {
            if (c.depth <= tolerance) continue;
            overlap = true;
            if (c.normal.y > 0) upwardContact = true;
        }
        if (overlap) { low = probe.y; break; }
        high = probe.y;
    }
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
