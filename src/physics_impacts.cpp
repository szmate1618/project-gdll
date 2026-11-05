#include "physics_impacts.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
PhysicsImpacts::PhysicsImpacts(b3WorldId world, const std::filesystem::path& storage) : world_(world) {
    for (const auto& path : acousticConfigFiles(storage))
        if (!profiles_.emplace(acousticConfigId(path), loadAcousticConfig(path)).second)
            throw std::runtime_error("Duplicate acoustic profile: " + path.string());
    b3World_SetHitEventThreshold(world_, .1f);
    events_.reserve(64);
}
void PhysicsImpacts::track(b3BodyId body, std::string_view profile, float scale) {
    if (!b3Body_IsValid(body) || !std::isfinite(scale) || scale <= 0)
        throw std::invalid_argument("Invalid acoustic body or scale");
    const auto found = profiles_.find(profile);
    // Bare synthetic/test worlds can use the crate default without resource I/O.
    // Runtime worlds with a catalog require a named profile, catching missing sidecars.
    if (found == profiles_.end() && !profiles_.empty())
        throw std::runtime_error("Missing acoustic profile: " + std::string(profile));
    bodies_[b3StoreBodyId(body)] = {found == profiles_.end() ? AcousticConfig{} : found->second, scale, 0};
    b3Body_EnableHitEvents(body, true);
}
void PhysicsImpacts::untrack(b3BodyId body) { bodies_.erase(b3StoreBodyId(body)); }
void PhysicsImpacts::capture() {
    ++tick_;
    const auto hits = b3World_GetContactEvents(world_);
    struct Candidate { b3BodyId body; b3ShapeId other; glm::vec3 position; float speed; };
    std::unordered_map<std::uint64_t, Candidate> candidates;
    for (int i = 0; i < hits.hitCount; ++i) {
        const auto& hit = hits.hitEvents[i];
        for (int side = 0; side < 2; ++side) {
            const auto shape = side ? hit.shapeIdB : hit.shapeIdA;
            const auto other = side ? hit.shapeIdA : hit.shapeIdB;
            if (!b3Shape_IsValid(shape) || !b3Shape_IsValid(other)) continue;
            const auto body = b3Shape_GetBody(shape);
            const auto key = b3StoreBodyId(body);
            const auto tracked = bodies_.find(key);
            if (tracked == bodies_.end() || (tracked->second.lastHit && tick_-tracked->second.lastHit < 8) ||
                hit.approachSpeed < tracked->second.config.minSpeed) continue;
            const auto found = candidates.find(key);
            if (found == candidates.end() || found->second.speed < hit.approachSpeed)
                candidates.insert_or_assign(key, Candidate{body, other,
                    {float(hit.point.x), float(hit.point.y), float(hit.point.z)}, hit.approachSpeed});
        }
    }
    for (const auto& [key, hit] : candidates) {
        auto& body = bodies_.at(key);
        body.lastHit = tick_;
        const auto other = b3Shape_GetBody(hit.other);
        float mass = b3Body_GetMass(hit.body);
        if (b3Body_GetType(other) == b3_dynamicBody) {
            const float otherMass = b3Body_GetMass(other);
            if (otherMass > 0) mass = mass * otherMass / (mass + otherMass);
        }
        const auto categories = b3Shape_GetFilter(hit.other).categoryBits;
        const bool hard = (categories & (hardImpactCategory | doorBodyCategory | vehicleBodyCategory)) != 0;
        PhysicsImpact event{makeImpactSound(body.config, mass, hit.speed, body.scale, hard, hit.position),
                            impactNoiseDb(body.config, mass, hit.speed)};
        if (events_.size() < 64) events_.push_back(event);
        else {
            const auto weakest = std::min_element(events_.begin(), events_.end(),
                [](const auto& a, const auto& b) { return a.sound.gain < b.sound.gain; });
            if (weakest->sound.gain < event.sound.gain) *weakest = event;
        }
    }
}
std::vector<PhysicsImpact> PhysicsImpacts::take() {
    std::vector<PhysicsImpact> result;
    result.swap(events_);
    return result;
}
} // namespace viewer
