#include "fps_controller.hpp"
#include "character_step.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
constexpr float tolerance = .0001f;
}

CollisionBox FPSController::proneBox(glm::vec3 feet, float yawDegrees) const {
    const float yaw = glm::radians(yawDegrees);
    const glm::vec3 forward(std::cos(yaw), 0, std::sin(yaw));
    CollisionBox box;
    box.halfExtents = {config_.proneWidth * .5f, stanceHeight(PlayerStance::Prone) * .5f,
                       config_.proneLength * .5f};
    // The eye/feet anchor is near the head: 20% of the head/torso length lies
    // ahead and 80% behind it. Legs are outside this movement approximation.
    box.center = feet - forward * (config_.proneLength * .3f) + glm::vec3(0, box.halfExtents.y, 0);
    box.rotation = glm::angleAxis(glm::radians(90.f - yawDegrees), glm::vec3(0, 1, 0));
    return box;
}

std::optional<CollisionBox> FPSController::collisionBox() const {
    if (stance_ != PlayerStance::Prone) return {};
    return proneBox(feet_, bodyYawDegrees_);
}

std::vector<Contact> FPSController::contactsAt(glm::vec3 feet, PlayerStance stance,
        const CollisionWorld& world, float skin) const {
    if (stance == PlayerStance::Prone) return world.contacts(proneBox(feet, bodyYawDegrees_), skin);
    return world.contacts(feet, config_.radius, stanceHeight(stance), skin);
}

std::optional<std::pair<float, glm::vec3>> FPSController::proneSupport(glm::vec3 feet,
        const CollisionWorld& world, float minY, float maxY) const {
    const auto box = proneBox(feet, bodyYawDegrees_);
    std::optional<std::pair<float, glm::vec3>> result;
    // A flat box can rest on either end of a slope or partly overhang a ledge.
    // Clearance against the full box still checks geometry between samples.
    for (float x : {-1.f, 0.f, 1.f}) for (float z : {-1.f, 0.f, 1.f}) {
        const auto point = box.center + box.rotation * ((box.halfExtents + glm::vec3(config_.skin)) * glm::vec3(x, -1, z));
        const auto hit = world.walkingGroundAt(point.x, point.z, minY, maxY);
        if (hit && walkable(hit->normal) && (!result || hit->height > result->first))
            result = std::pair<float, glm::vec3>{hit->height, hit->normal};
    }
    return result;
}

void FPSController::turnBody(float yawDegrees, const CollisionWorld& world) {
    const float target = std::remainder(yawDegrees, 360.f);
    const float delta = std::remainder(target - bodyYawDegrees_, 360.f);
    const float reach = std::hypot(config_.proneWidth * .5f, config_.proneLength * .8f);
    // Bound the entire rotation interval with a box at its midpoint expanded
    // horizontally by the greatest corner travel. Subdivide blocked intervals
    // to approach walls without either snapping through them or moving feet.
    const auto advance = [&](const auto& self, float from, float to, int depth) -> float {
        const float middle = (from + to) * .5f;
        auto swept = proneBox(feet_, middle);
        const float margin = reach * std::abs(glm::radians(to - from)) * .5f;
        swept.halfExtents.x += margin;
        swept.halfExtents.z += margin;
        const auto contacts = world.contacts(swept, config_.skin);
        float lift = 0;
        if (grounded_) for (const auto& c : contacts)
            if (c.depth > tolerance && walkable(c.normal)) lift = std::max(lift, c.depth / c.normal.y);
        bool blocked = lift > config_.stepHeight;
        if (!blocked && lift > 0) {
            // Bound the upward motion too. Only supporting walkable surfaces
            // may intersect this envelope; a ceiling or side blocks the turn.
            auto rising = swept;
            rising.center.y += lift * .5f;
            rising.halfExtents.y += lift * .5f;
            for (const auto& c : world.contacts(rising, config_.skin))
                if (c.depth > tolerance && !walkable(c.normal)) blocked = true;
            swept.center.y += lift;
        }
        if (!blocked) for (const auto& c : world.contacts(swept, config_.skin))
            if (c.depth > tolerance) blocked = true;
        if (!blocked) { feet_.y += lift; return to; }
        if (depth == 14 || margin < tolerance) return from;
        const float first = self(self, from, middle, depth + 1);
        if (std::abs(first - middle) > .00001f) return first;
        return self(self, middle, to, depth + 1);
    };
    if (std::abs(delta) > .00001f) {
        bodyYawDegrees_ = std::remainder(advance(advance, bodyYawDegrees_, bodyYawDegrees_ + delta, 0), 360.f);
        if (grounded_) snapToGround(world, config_.groundSnap);
    }
}

bool FPSController::tryStep(glm::vec3 previous, glm::vec3 requested, const CollisionWorld& world) {
    if (stance_ != PlayerStance::Prone) {
        const CharacterStepConfig stepConfig{config_.radius, bodyHeight(), config_.stepHeight,
            std::cos(glm::radians(config_.maxSlopeDegrees)), config_.skin};
        const auto stepped = tryCharacterStep(previous, requested, world, stepConfig);
        if (!stepped) return false;
        feet_ = stepped->feet;
        groundNormal_ = stepped->groundNormal;
    } else {
        if (config_.stepHeight <= 0) return false;
        const auto contacts = contactsAt(requested, stance_, world, config_.skin);
        if (std::none_of(contacts.begin(), contacts.end(), [](const Contact& c) { return c.depth > tolerance; }))
            return false;
        const float maximum = previous.y + config_.stepHeight + config_.skin;
        auto support = proneSupport(requested, world, previous.y + tolerance, maximum);
        const glm::vec3 offset(requested.x - previous.x, 0, requested.z - previous.z);
        const float distance = glm::length(offset);
        if (distance < .000001f) return false;
        const auto forward = offset / distance;
        for (const auto& contact : contacts) {
            const auto sample = contact.point + forward * .005f;
            const auto hit = world.walkingGroundAt(sample.x, sample.z, previous.y + tolerance, maximum);
            if (hit && walkable(hit->normal) && (!support || hit->height > support->first))
                support = std::pair<float, glm::vec3>{hit->height, hit->normal};
        }
        if (!support) return false;
        auto landing = requested;
        landing.y = support->first + config_.skin;
        if (landing.y <= previous.y + tolerance || landing.y > maximum) return false;
        const int lifts = std::max(1, int(std::ceil((landing.y - previous.y) / .02f)));
        for (int i = 1; i <= lifts; ++i) {
            auto lifted = previous;
            lifted.y += (landing.y - previous.y) * (float(i) / lifts);
            for (const auto& c : contactsAt(lifted, stance_, world, config_.skin))
                if (c.depth > tolerance && c.normal.y <= 0) return false;
        }
        for (const auto& c : contactsAt(landing, stance_, world, config_.skin))
            if (c.depth > tolerance) return false;
        feet_ = landing;
        groundNormal_ = support->second;
    }
    grounded_ = true;
    verticalVelocity_ = 0;
    return true;
}
} // namespace viewer
