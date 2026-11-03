#include "zombie_doorway.hpp"

#include "collision_world.hpp"
#include "zombie_crowd.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace viewer {
namespace {
constexpr float queryRange = 4.f;
constexpr float clearedDistance = .8f, stallSeconds = 2.f;
constexpr std::size_t participantLimit = ZombieCrowd::neighborBudget;

float horizontalSquared(glm::vec3 offset) { return offset.x * offset.x + offset.z * offset.z; }
bool finite(glm::vec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
float entryDistance(glm::vec3 feet, const DoorwayAperture& door, float side) {
    return glm::dot(feet - door.center, door.outward) * side;
}
float progress(glm::vec3 feet, const DoorwayAperture& door, float side) {
    return entryDistance(feet, door, side) + std::abs(glm::dot(feet - door.center, door.right));
}
}

std::size_t ZombieDoorwayGuidance::Hash::operator()(const Key& key) const {
    const auto a = std::hash<std::uint64_t>{}(key.source), b = std::hash<std::size_t>{}(key.door);
    return a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
}

void ZombieDoorwayGuidance::reset() {
    requests_.clear(); passages_.clear(); step_ = 0; seconds_ = 0;
}

void ZombieDoorwayGuidance::erase(std::size_t id) {
    const auto found = requests_.find(id);
    if (found == requests_.end()) return;
    auto passage = passages_.find(found->second.key);
    if (passage != passages_.end()) {
        auto& members = passage->second.members;
        members[found->second.slot] = members.back();
        requests_.at(members.back()).slot = found->second.slot;
        members.pop_back();
        if (passage->second.owner == id) passage->second.owner.reset();
        if (members.empty()) passages_.erase(passage);
    }
    requests_.erase(found);
}

void ZombieDoorwayGuidance::beginStep(float seconds) {
    ++step_;
    if (std::isfinite(seconds) && seconds > 0) seconds_ += seconds;
    for (auto it = requests_.begin(); it != requests_.end();) {
        const auto id = it->first;
        const bool stale = it->second.step + 1 < step_;
        ++it;
        if (stale) erase(id);
    }
}

void ZombieDoorwayGuidance::reserve(Passage& passage, std::size_t owner) {
    passage.owner = owner;
    passage.committed = false;
    const auto& request = requests_.at(owner);
    passage.bestProgress = progress(request.feet, passage.opening, request.entrySide);
    passage.progressSeconds = seconds_;
}

std::optional<glm::vec3> ZombieDoorwayGuidance::destination(
        std::size_t id, glm::vec3 feet, glm::vec3 target, const CollisionWorld& world) {
    if (!finite(feet) || !finite(target)) { erase(id); return std::nullopt; }
    const auto doors = world.nearbyDoorways(feet, queryRange);
    std::optional<DoorwayAperture> selected;
    float side = 0, best = std::numeric_limits<float>::max();
    const auto previous = requests_.find(id);
    for (const auto& door : doors) {
        if (door.width < ZombieCrowd::bodyRadius * 2 + .12f ||
            feet.y < door.bottom - .3f || feet.y + ZombieCrowd::bodyHeight > door.top + .02f) continue;
        const float from = glm::dot(feet - door.center, door.outward);
        const float to = glm::dot(target - door.center, door.outward);
        const Key key{door.source, door.door};
        const bool retained = previous != requests_.end() && previous->second.key == key;
        // A closing panel releases its crossing reservation, but an existing
        // queue keeps yielding outside the swing instead of pinning it shut.
        if (!door.open && !retained && passages_.find(key) == passages_.end()) continue;
        const float approachSide = retained ? previous->second.entrySide : (from >= 0 ? 1.f : -1.f);
        if (to * approachSide >= -.05f || from * approachSide < -clearedDistance) continue;
        const float across = std::abs(glm::dot(feet - door.center, door.right));
        // This is a local funnel for a nearby aperture along the intended
        // approach, not a search for entrances around the rest of a building.
        const float fraction = std::abs(from - to) > 1e-5f ? from / (from - to) : 0;
        const auto crossing = feet + (target - feet) * fraction;
        if (std::abs(glm::dot(crossing - door.center, door.right)) > door.width * .5f + 1.f ||
            across > door.width * .5f + 1.5f || horizontalSquared(feet - door.center) > queryRange * queryRange) continue;
        const float score = horizontalSquared(feet - door.center);
        if (retained || score < best || (score == best && selected &&
                std::make_pair(door.source, door.door) < std::make_pair(selected->source, selected->door))) {
            selected = door; side = approachSide; best = score;
            if (retained) break;
        }
    }
    if (!selected) { erase(id); return std::nullopt; }
    const Key key{selected->source, selected->door};
    if (previous != requests_.end() && !(previous->second.key == key)) erase(id);
    auto [passageIt, inserted] = passages_.try_emplace(key);
    auto& passage = passageIt->second;
    if (inserted) passage.opening = *selected;
    // Streamed slots can acquire new geometry without changing the door ID.
    if (passage.opening.center != selected->center || passage.opening.outward != selected->outward ||
        passage.opening.width != selected->width) {
        const auto members = passage.members;
        for (const auto member : members) erase(member);
        return std::nullopt;
    }
    auto requestIt = requests_.find(id);
    if (requestIt == requests_.end()) {
        if (passage.members.size() == participantLimit) return std::nullopt;
        const float across = glm::dot(feet - selected->center, selected->right);
        const float yieldSide = std::abs(across) > .05f ? (across > 0 ? 1.f : -1.f) : (id % 2 ? 1.f : -1.f);
        requestIt = requests_.emplace(id, Request{key, feet, side, yieldSide, step_, 0, passage.members.size()}).first;
        passage.members.push_back(id);
    }
    auto& request = requestIt->second;
    request.feet = feet; request.step = step_;
    if (!selected->open) passage.owner.reset();

    if (passage.owner == id) {
        const float current = progress(feet, *selected, side);
        if (current < passage.bestProgress - .02f) {
            passage.bestProgress = current;
            passage.progressSeconds = seconds_;
        } else if (seconds_ - passage.progressSeconds >= stallSeconds) {
            request.yieldUntil = seconds_ + 1.;
            passage.owner.reset();
        }
    }
    std::optional<std::size_t> nearest;
    float nearestScore = std::numeric_limits<float>::max();
    for (const auto member : passage.members) {
        const auto& candidate = requests_.at(member);
        if (candidate.yieldUntil > seconds_) continue;
        const float score = horizontalSquared(candidate.feet - selected->center);
        if (score < nearestScore || (score == nearestScore && (!nearest || member < *nearest))) {
            nearest = member; nearestScore = score;
        }
    }
    if (selected->open && nearest && (!passage.owner || (!passage.committed && nearestScore + .01f <
            horizontalSquared(requests_.at(*passage.owner).feet - selected->center)))) reserve(passage, *nearest);

    // Center outside the entire panel sweep before entering. This works for
    // both swing directions and avoids cutting diagonally across the free edge.
    const float approachDistance = std::max(1.f, selected->width + ZombieCrowd::bodyRadius + .2f);
    const float exitDistance = approachDistance;
    glm::vec3 destination;
    if (passage.owner == id) {
        const float along = entryDistance(feet, *selected, side);
        const float across = std::abs(glm::dot(feet - selected->center, selected->right));
        const float laneHalfWidth = std::max(.02f, selected->width * .5f - ZombieCrowd::bodyRadius - .06f);
        const bool aligned = across <= laneHalfWidth && along <= approachDistance + .05f;
        if (aligned) passage.committed = true;
        destination = selected->center + selected->outward * side * (passage.committed ? -exitDistance : approachDistance);
        if (passage.committed && std::abs(glm::dot(target - selected->center, selected->right)) <= laneHalfWidth &&
            -glm::dot(target - selected->center, selected->outward) * side < exitDistance)
            destination = target; // A close remembered goal must not be overshot by the exit hint.
    } else {
        // Yield away from the center line, including backing out if already
        // beside the winner at the threshold. Bodies never overlap or teleport.
        std::size_t rank = 0;
        const float score = horizontalSquared(feet - selected->center);
        for (const auto member : passage.members) {
            if (passage.owner == member || member == id) continue;
            const auto& other = requests_.at(member);
            if (other.entrySide != side) continue;
            const float otherScore = horizontalSquared(other.feet - selected->center);
            if (otherScore < score || (otherScore == score && member < id)) ++rank;
        }
        const float back = std::min(3.5f, approachDistance + .7f + float(rank) * .7f);
        const float lateral = selected->width * .5f + ZombieCrowd::bodyRadius + .15f;
        destination = selected->center + selected->outward * side * back + selected->right * request.yieldSide * lateral;
    }
    destination.y = feet.y;
    return destination;
}

std::optional<glm::vec3> ZombieCrowd::doorwayDestination(std::size_t id, glm::vec3 target,
                                                       const CollisionWorld& world) {
    if (id >= bodies_.size() || !bodies_[id].present) return std::nullopt;
    return doorways_.destination(id, bodies_[id].feet, target, world);
}

} // namespace viewer
