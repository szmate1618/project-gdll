#include "bird_flock.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
constexpr float flockRadius = 7.f;
constexpr float cruiseSpeed = 8.f;
constexpr float attackSpeed = 17.f;
constexpr float sightRange = 85.f;
constexpr float recoverySeconds = 7.f;
constexpr float birdRadius = .14f;

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float squared(glm::vec3 value) { return glm::dot(value, value); }
glm::vec3 limited(glm::vec3 value, float maximum) {
    const float length2 = squared(value);
    return length2 > maximum * maximum ? value * (maximum / std::sqrt(length2)) : value;
}
glm::vec3 direction(glm::vec3 value, glm::vec3 fallback = {0,0,1}) {
    const float length2 = squared(value);
    return length2 > 1e-8f ? value / std::sqrt(length2) : fallback;
}
bool validPlayer(const BirdFlockPlayer& player) {
    return player.targetable && finite(player.feet) && std::isfinite(player.radius) &&
        std::isfinite(player.height) && player.radius > 0 && player.height >= 2 * player.radius;
}
// Closest points of two finite line segments (including degenerate segments).
float segmentDistanceSquared(glm::vec3 p, glm::vec3 q, glm::vec3 a, glm::vec3 b) {
    const auto u = q - p, v = b - a, w = p - a;
    const float uu = squared(u), vv = squared(v), uv = glm::dot(u,v);
    const float uw = glm::dot(u,w), vw = glm::dot(v,w);
    float s = 0, t = 0;
    if (uu <= 1e-12f) t = vv > 1e-12f ? std::clamp(vw / vv, 0.f, 1.f) : 0;
    else if (vv <= 1e-12f) s = std::clamp(-uw / uu, 0.f, 1.f);
    else {
        const float denominator = uu * vv - uv * uv;
        if (denominator > 1e-12f) s = std::clamp((uv * vw - uw * vv) / denominator, 0.f, 1.f);
        t = (uv * s + vw) / vv;
        if (t < 0) { t = 0; s = std::clamp(-uw / uu, 0.f, 1.f); }
        else if (t > 1) { t = 1; s = std::clamp((uv - uw) / uu, 0.f, 1.f); }
    }
    return squared(w + u * s - v * t);
}
}

bool birdHitsPlayer(glm::vec3 from, glm::vec3 to, const BirdFlockPlayer& player, float radius) {
    if (!finite(from) || !finite(to) || !validPlayer(player) || !std::isfinite(radius) || radius < 0) return false;
    const auto a = player.feet + glm::vec3(0, player.radius, 0);
    const auto b = player.feet + glm::vec3(0, player.height - player.radius, 0);
    const float combined = player.radius + radius;
    return segmentDistanceSquared(from, to, a, b) <= combined * combined;
}

float BirdFlocks::randomUnit() {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return static_cast<float>(random_ >> 8) * (1.f / 16777216.f);
}

glm::vec3 BirdFlocks::randomOffset(float radius, float height) {
    glm::vec3 offset((randomUnit() * 2 - 1) * radius,
                     (randomUnit() * 2 - 1) * height,
                     (randomUnit() * 2 - 1) * radius);
    const float horizontal = offset.x * offset.x + offset.z * offset.z;
    if (horizontal > radius * radius) {
        const float scale = radius / std::sqrt(horizontal);
        offset.x *= scale; offset.z *= scale;
    }
    return offset;
}

bool BirdFlocks::waypoint(glm::vec3 point, float radius) {
    ++stats_.waypointChecks;
    if (!finite(point) || point.x < minimum_.x + radius || point.x > maximum_.x - radius ||
        point.z < minimum_.z + radius || point.z > maximum_.z - radius ||
        point.y < minimum_.y + radius || point.y > maximum_.y - radius) return false;
    return space_.clearPoint(point, radius);
}
bool BirdFlocks::corridor(glm::vec3 from, glm::vec3 to, float radius) {
    ++stats_.segmentChecks;
    return space_.clearSegment(from, to, radius);
}

BirdFlocks::BirdFlocks(const CollisionWorld& world, const TreeLayer* trees,
                       glm::vec3 boundsMin, glm::vec3 boundsMax, glm::vec3 preferredCenter,
                       BirdFlockSettings settings)
    : space_(world, trees), minimum_(boundsMin), maximum_(boundsMax), preferred_(preferredCenter),
      random_(settings.seed ? settings.seed : 0x62697264) {
    if (!finite(boundsMin) || !finite(boundsMax) || !finite(preferredCenter) ||
        glm::any(glm::lessThan(boundsMax, boundsMin)))
        throw std::invalid_argument("Bird flock bounds and center must be finite, ordered world meters");
    if (settings.flockCount > 64 || settings.birdsPerFlock > 4096 ||
        (settings.flockCount && settings.birdsPerFlock > 65536 / settings.flockCount))
        throw std::invalid_argument("Bird flocks are limited to 64 flocks and 65536 birds");
    if (!settings.flockCount || !settings.birdsPerFlock) return;
    // The map's highest roof is not an altitude ceiling. Leave a modest sky
    // band above it, while deriving all horizontal limits from this map.
    maximum_.y += 45;
    const auto extent = maximum_ - minimum_;
    if (extent.x <= flockRadius * 2 || extent.z <= flockRadius * 2) return;
    preferred_ = glm::clamp(preferred_, minimum_, maximum_);
    birds_.reserve(settings.flockCount * settings.birdsPerFlock);
    birdData_.reserve(settings.flockCount * settings.birdsPerFlock);
    flocks_.reserve(settings.flockCount);
    flockData_.reserve(settings.flockCount);
    for (std::size_t flock = 0; flock < settings.flockCount; ++flock) {
        glm::vec3 center(0);
        bool found = false;
        for (int attempt = 0; attempt < 32; ++attempt) {
            center = preferred_ + randomOffset(35.f + 18.f * static_cast<float>(flock), 10);
            center.y += 18 + 8 * randomUnit();
            center = glm::clamp(center, minimum_ + glm::vec3(flockRadius), maximum_ - glm::vec3(flockRadius));
            if (waypoint(center, flockRadius)) { found = true; break; }
        }
        // An obstructed preferred area may still have open sky above it. One
        // bounded fallback avoids repeated construction work in dense towns.
        if (!found) {
            center.y = maximum_.y - flockRadius - 1;
            found = waypoint(center, flockRadius);
        }
        if (!found) continue;
        BirdFlockState state;
        state.firstBird = birds_.size();
        state.birdCount = settings.birdsPerFlock;
        state.center = state.target = center;
        state.velocity = direction(randomOffset(1, .15f)) * cruiseSpeed;
        FlockData data;
        data.nextWaypoint = seconds_;
        data.nextAttack = 1.5f + static_cast<float>(flock) * .6f;
        flocks_.push_back(state);
        flockData_.push_back(data);
        for (std::size_t i = 0; i < settings.birdsPerFlock; ++i) {
            BirdData individual;
            individual.offset = randomOffset(4.8f, 2.0f);
            birds_.push_back({center + individual.offset, state.velocity, randomUnit() * 6.2831853f});
            birdData_.push_back(individual);
        }
        chooseWander(flocks_.size() - 1);
    }
    stats_ = {};
}

bool BirdFlocks::chooseWander(std::size_t index) {
    auto& flock = flocks_[index];
    auto& data = flockData_[index];
    // Bounded sampling is shared by the flock. Failed routes leave a short
    // retry delay rather than doing scene queries again for every bird/frame.
    for (int attempt = 0; attempt < 4; ++attempt) {
        auto point = flock.center + randomOffset(65, 14);
        point = glm::clamp(point, minimum_ + glm::vec3(flockRadius), maximum_ - glm::vec3(flockRadius));
        point.y = std::max(point.y, minimum_.y + 12);
        if (squared(point - flock.center) < 100) continue;
        if (waypoint(point, flockRadius) && corridor(flock.center, point, flockRadius)) {
            flock.target = point;
            data.routeActive = true;
            data.nextWaypoint = seconds_ + 6 + randomUnit() * 4;
            return true;
        }
    }
    flock.target = flock.center;
    data.routeActive = false;
    data.nextWaypoint = seconds_ + 1;
    return false;
}

void BirdFlocks::recover(std::size_t index) {
    auto& flock = flocks_[index];
    auto& data = flockData_[index];
    flock.mode = BirdFlockMode::recovering;
    flock.secondsRemaining = recoverySeconds;
    data.nextAttack = seconds_ + recoverySeconds + 3;
    // Keep the checked exit as the first recovery target. Normal wandering
    // resumes after the birds have climbed away from the player's head.
    flock.target = data.exit;
    data.routeActive = true;
    data.nextWaypoint = seconds_ + 2;
}

void BirdFlocks::detectPlayer(const BirdFlockPlayer& player) {
    if (birds_.empty() || !validPlayer(player)) return;
    const auto chosen = static_cast<std::size_t>(randomUnit() * static_cast<float>(birds_.size()));
    std::size_t index = 0;
    while (index + 1 < flocks_.size() && chosen >= flocks_[index].firstBird + flocks_[index].birdCount) ++index;
    auto& flock = flocks_[index];
    auto& data = flockData_[index];
    if (flock.mode != BirdFlockMode::wandering || seconds_ < data.nextAttack) return;
    const auto aim = player.feet + glm::vec3(0, std::min(1.45f, player.height - .15f), 0);
    const auto offset = aim - birds_[chosen].position;
    if (squared(offset) > sightRange * sightRange || squared(offset) < 9) return;
    ++stats_.sightChecks;
    if (!space_.lineOfSight(birds_[chosen].position, aim)) return;
    // Snapshot the target now: dodging during the gather or dive is useful.
    const auto attackDirection = direction(aim - flock.center);
    const auto horizontal = direction(glm::vec3(attackDirection.x, 0, attackDirection.z));
    auto exit = aim + horizontal * 20.f + glm::vec3(0, 9.f, 0);
    if (!waypoint(aim, .2f) || !waypoint(exit, 1.f) ||
        !corridor(flock.center, aim, .2f) || !corridor(aim, exit, .2f)) {
        data.nextAttack = seconds_ + 1;
        return;
    }
    data.aim = aim;
    data.exit = exit;
    data.attackDirection = horizontal;
    ++data.pass;
    for (std::size_t i = flock.firstBird; i < flock.firstBird + flock.birdCount; ++i)
        birdData_[i].passedPlayer = false;
    flock.mode = BirdFlockMode::gathering;
    flock.secondsRemaining = .85f;
    flock.target = exit;
}

unsigned BirdFlocks::update(float dt, const BirdFlockPlayer& player) {
    stats_ = {};
    if (!std::isfinite(dt) || dt <= 0 || birds_.empty()) return 0;
    dt = std::min(dt, .1f);
    seconds_ += dt;
    detectPlayer(player);
    const bool playerValid = validPlayer(player);
    for (std::size_t index = 0; index < flocks_.size(); ++index) {
        auto& flock = flocks_[index];
        auto& data = flockData_[index];
        if (flock.mode == BirdFlockMode::gathering || flock.mode == BirdFlockMode::swooping ||
            flock.mode == BirdFlockMode::recovering) {
            flock.secondsRemaining -= dt;
            if (flock.secondsRemaining <= 0) {
                if (flock.mode == BirdFlockMode::gathering) {
                    flock.mode = BirdFlockMode::swooping;
                    flock.secondsRemaining = std::clamp(glm::length(data.aim - flock.center) / attackSpeed + 2.5f, 3.f, 9.f);
                } else if (flock.mode == BirdFlockMode::swooping) recover(index);
                else { flock.mode = BirdFlockMode::wandering; data.nextWaypoint = seconds_; }
            }
        }
        if ((flock.mode == BirdFlockMode::wandering || flock.mode == BirdFlockMode::recovering) &&
            (seconds_ >= data.nextWaypoint || (data.routeActive && squared(flock.target - flock.center) < 16))) chooseWander(index);

        glm::vec3 meanPosition(0), meanVelocity(0);
        const auto end = flock.firstBird + flock.birdCount;
        for (std::size_t i = flock.firstBird; i < end; ++i) {
            meanPosition += birds_[i].position;
            meanVelocity += birds_[i].velocity;
        }
        const float inverseCount = 1.f / static_cast<float>(flock.birdCount);
        meanPosition *= inverseCount;
        meanVelocity *= inverseCount;
        flock.center = meanPosition;
        flock.velocity = meanVelocity;
        const bool attack = flock.mode == BirdFlockMode::swooping;
        const bool gathering = flock.mode == BirdFlockMode::gathering;
        const float speed = attack ? attackSpeed : (gathering ? 2.5f : cruiseSpeed);
        const float slotScale = attack || gathering ? .48f : 1.f;
        auto desired = direction(flock.target - meanPosition, direction(meanVelocity)) * speed;
        if (gathering) desired = direction(meanVelocity) * speed;
        const bool holding = !attack && !gathering && squared(flock.target - meanPosition) < 9;
        if (holding) desired = glm::vec3(0);
        for (std::size_t i = flock.firstBird; i < end; ++i) {
            auto& bird = birds_[i];
            auto& individual = birdData_[i];
            const auto previous = bird.position;
            glm::vec3 acceleration(0);
            if (attack) {
                // Each bird crosses the snapshotted plane once, then pulls up.
                // It never circles back to the aim or tracks a moving player.
                if (glm::dot(bird.position - data.aim, data.attackDirection) >= 0)
                    individual.passedPlayer = true;
                auto slot = individual.offset * slotScale;
                slot.y *= .2f;
                const auto target = (individual.passedPlayer ? data.exit : data.aim) + slot;
                acceleration = (direction(target - bird.position, data.attackDirection) * speed - bird.velocity) * 5.f;
            } else {
                const auto slot = meanPosition + individual.offset * slotScale;
                acceleration = (desired - bird.velocity) * 2.f + (meanVelocity - bird.velocity) * .45f +
                    (slot - bird.position) * (gathering ? 5.f : 1.4f);
            }
            // Four bounded samples replace quadratic all-pairs separation. The
            // coherent flock slots handle the remaining broad crowd spacing.
            const auto local = i - flock.firstBird;
            for (std::size_t sample = 1; sample <= 4 && sample < flock.birdCount; ++sample) {
                const auto neighbor = flock.firstBird + (local + sample * 37) % flock.birdCount;
                if (neighbor == i) continue;
                const auto away = bird.position - birds_[neighbor].position;
                const float distance2 = squared(away);
                if (distance2 > 1e-5f && distance2 < 1.f) acceleration += away * (1.8f / (distance2 + .1f));
            }
            bird.velocity += limited(acceleration, attack ? 60.f : 18.f) * dt;
            bird.velocity = limited(bird.velocity, speed * 1.2f);
            bird.position += bird.velocity * dt;
            if (playerValid && individual.hitPass != data.pass && seconds_ >= individual.nextHit &&
                birdHitsPlayer(previous, bird.position, player, birdRadius)) {
                // Only a rare actual contact needs a fresh scene ray, so taking
                // cover after acquisition still prevents damage through walls.
                const auto torso = player.feet + glm::vec3(0, player.height * .5f, 0);
                if (space_.clearPoint(torso, .01f) && space_.lineOfSight(previous, torso)) {
                    individual.hitPass = data.pass;
                    individual.nextHit = seconds_ + 2;
                    ++stats_.hits;
                }
            }
        }
    }
    return static_cast<unsigned>(stats_.hits);
}
} // namespace viewer
