#include "zombie_lunge.hpp"
#include "zombie_layer.hpp"
#include "zombie_movement.hpp"
#include "zombie_chase.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
constexpr float pullbackDistance = .08f;
constexpr float movementSegment = .05f;
constexpr float tolerance = .0001f;

float horizontalDistance(glm::vec3 a, glm::vec3 b) {
    return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
}

// Upright body cylinders match the living crowd's cheap physical bodies. The
// vertical interval is intersected with the horizontal sweep, so jumps, current
// stances and separate floors do not become unconditional horizontal hits.
std::optional<float> playerContact(glm::vec3 start, glm::vec3 end, const ZombiePlayer& player) {
    const glm::dvec2 offset(double(start.x) - player.feet.x, double(start.z) - player.feet.z);
    const glm::dvec2 delta(double(end.x) - start.x, double(end.z) - start.z);
    const double radius = double(ZombieCrowd::bodyRadius) + player.radius;
    const double c = glm::dot(offset, offset) - radius * radius;
    const double along = glm::dot(offset, delta), a = glm::dot(delta, delta);
    if (c <= 0 && along >= 0) return std::nullopt; // Permit recovery out of an old overlap.
    double enter = 0, leave = 1;
    if (a > 1e-16) {
        const double discriminant = along * along - a * c;
        if (discriminant < 0) return std::nullopt;
        const double root = std::sqrt(discriminant);
        enter = std::max(enter, (-along - root) / a);
        leave = std::min(leave, (-along + root) / a);
    } else if (c > 0) return std::nullopt;
    const double dy = double(end.y) - start.y;
    const double low = double(player.feet.y) - ZombieCrowd::bodyHeight;
    const double high = double(player.feet.y) + player.height;
    if (std::abs(dy) < 1e-12) {
        if (start.y <= low || start.y >= high) return std::nullopt;
    } else {
        const double first = (low - start.y) / dy, second = (high - start.y) / dy;
        enter = std::max(enter, std::min(first, second));
        leave = std::min(leave, std::max(first, second));
    }
    if (enter > leave || leave < 0 || enter > 1) return std::nullopt;
    return static_cast<float>(std::max(0.0, enter));
}

struct LungeMovement {
    glm::vec3 feet{0}, impactVelocity{0};
    float travel = 0;
    const ZombiePlayer* contact = nullptr;
    bool blocked = false;
};

LungeMovement move(glm::vec3 start, glm::vec3 desired, float speed,
                   const CollisionWorld& world, ZombieCrowd& crowd, std::size_t zombie,
                   const ZombiePlayer* players, std::size_t playerCount) {
    LungeMovement result;
    result.feet = start;
    const float distance = horizontalDistance(start, desired);
    if (distance < tolerance) return result;
    const auto direction = glm::vec3(desired.x - start.x, 0, desired.z - start.z) / distance;
    float remaining = distance;
    std::size_t crowdChecks = 0;
    // Inspect accepted short scene/crowd segments, never the rejected requested
    // path. This keeps a wall in front of a player authoritative for the hit.
    for (int segment = 0; segment < 64 && remaining > tolerance; ++segment) {
        const float step = std::min(remaining, movementSegment);
        const auto previous = result.feet;
        const auto requested = previous + direction * step;
        const auto accepted = moveZombieBody(previous, requested, world, &crowd, zombie,
            nullptr, nullptr, &crowdChecks).feet;
        float fraction = 1;
        for (std::size_t i = 0; i < playerCount; ++i) {
            const auto hit = playerContact(previous, accepted, players[i]);
            if (hit && (*hit < fraction || (*hit == fraction &&
                    (!result.contact || players[i].id < result.contact->id)))) {
                fraction = *hit;
                result.contact = &players[i];
            }
        }
        if (result.contact) {
            // A walking step can raise accepted.y in one segment. Its linear
            // chord is not necessarily supported or clear at an intermediate
            // point; solve the shortened request again before publishing it.
            const auto clipped = glm::mix(previous, accepted, fraction);
            result.feet = moveZombieBody(previous, clipped, world, &crowd, zombie,
                nullptr, nullptr, &crowdChecks).feet;
            for (std::size_t i = 0; i < playerCount; ++i) {
                const auto& body = players[i];
                if (result.feet.y >= body.feet.y + body.height ||
                        result.feet.y + ZombieCrowd::bodyHeight <= body.feet.y) continue;
                const float clearance = std::min(ZombieCrowd::bodyRadius + body.radius,
                    horizontalDistance(previous, body.feet));
                if (horizontalDistance(result.feet, body.feet) + tolerance < clearance) {
                    // A scene/crowd correction of the shortened request must
                    // not introduce penetration into any current player body.
                    result.feet = previous;
                    result.contact = nullptr;
                    result.blocked = true;
                    return result;
                }
            }
            result.travel += horizontalDistance(previous, result.feet);
            const auto& player = *result.contact;
            const float separation = horizontalDistance(result.feet, player.feet);
            const bool touching = separation <= ZombieCrowd::bodyRadius + player.radius + tolerance &&
                result.feet.y < player.feet.y + player.height &&
                result.feet.y + ZombieCrowd::bodyHeight > player.feet.y;
            if (!touching) {
                result.contact = nullptr;
                result.blocked = true; // The shortened, validated path did not reach that body.
                return result;
            }
            // Use the incoming speed before contact clipped the step or stopped
            // the body. Otherwise the last fraction of a frame changes damage.
            const auto incoming = accepted - previous;
            const float incomingLength = glm::length(incoming);
            result.impactVelocity = incomingLength > 1e-8f ? incoming * (speed / incomingLength) : glm::vec3(0);
            return result;
        }
        result.feet = accepted;
        result.travel += horizontalDistance(previous, result.feet);
        remaining -= step;
        if (horizontalDistance(accepted, requested) > tolerance) {
            result.blocked = true;
            return result; // A strike stops at an obstruction instead of steering around it.
        }
    }
    return result;
}

void commit(ZombieInstance& instance, glm::vec3 feet, float seconds) {
    const auto previous = glm::vec3(instance.restRoot[3]);
    stopZombieLocomotion(instance);
    instance.running = false;
    instance.restRoot[3] = glm::vec4(feet, 1);
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    instance.locomotionVelocity = seconds > 0 ? (feet - previous) / seconds : glm::vec3(0);
}

void recover(ZombieLunge& lunge, const ZombieInstance& instance) {
    lunge.phase = ZombieLunge::Phase::recovering;
    lunge.elapsed = 0;
    lunge.returnSpeed = horizontalDistance(glm::vec3(instance.restRoot[3]), lunge.origin) /
        zombieAttackRecoverySeconds;
}
}

bool beginZombieLunge(ZombieLunge& lunge, ZombieInstance& instance,
                      const ZombiePlayer& player, double now) {
    if (lunge.phase != ZombieLunge::Phase::ready || now < lunge.nextAttackSeconds)
        return false;
    const auto feet = glm::vec3(instance.restRoot[3]);
    const glm::vec2 offset(player.feet.x - feet.x, player.feet.z - feet.z);
    const float length = glm::length(offset);
    if (!std::isfinite(length) || length < 1e-5f) return false;
    lunge.origin = feet;
    lunge.direction = {offset.x / length, 0, offset.y / length};
    lunge.targetPlayer = player.id;
    lunge.elapsed = lunge.forwardTravel = 0;
    lunge.strikeStopped = false;
    lunge.phase = ZombieLunge::Phase::windup;
    lunge.nextAttackSeconds = now + zombieAttackWindupSeconds + zombieLungeSeconds +
        zombieAttackRecoverySeconds + zombieAttackCooldownSeconds;
    commit(instance, feet, 0);
    return true;
}

void cancelZombieLunge(ZombieLunge& lunge, double now) {
    lunge.phase = ZombieLunge::Phase::ready;
    lunge.nextAttackSeconds = std::max(lunge.nextAttackSeconds, now + zombieAttackCooldownSeconds);
}

void stepZombieLunge(ZombieLunge& lunge, ZombieInstance& instance,
                     const CollisionWorld& world, ZombieCrowd& crowd, std::size_t zombie,
                     const ZombiePlayer* players, std::size_t playerCount,
                     float seconds, double now, std::vector<ZombieAttackHit>& hits,
                     std::size_t& hitRays) {
    if (lunge.phase == ZombieLunge::Phase::ready) return;
    if (instance.removed || instance.ragdoll) { cancelZombieLunge(lunge, now); return; }
    if (!std::isfinite(seconds) || seconds <= 0) return;
    float remaining = seconds;
    // Carry a partial step across phase boundaries. One update can finish this
    // cycle, but never starts another, even after an unusually large timestep.
    for (int phase = 0; phase < 3 && remaining > 1e-7f; ++phase) {
        const auto feet = glm::vec3(instance.restRoot[3]);
        if (lunge.phase == ZombieLunge::Phase::windup) {
            const float duration = std::min(remaining, std::max(0.f, zombieAttackWindupSeconds - lunge.elapsed));
            lunge.elapsed += duration;
            turnZombieToward(instance, {lunge.direction.x, lunge.direction.z}, duration);
            const auto desired = lunge.origin - lunge.direction *
                (pullbackDistance * std::min(1.f, lunge.elapsed / zombieAttackWindupSeconds));
            const auto movement = move(feet, desired, pullbackDistance / zombieAttackWindupSeconds,
                world, crowd, zombie, players, playerCount);
            commit(instance, movement.feet, duration);
            remaining -= duration;
            if (lunge.elapsed + 1e-6f < zombieAttackWindupSeconds) return;
            lunge.phase = ZombieLunge::Phase::striking;
            lunge.elapsed = 0;
            continue;
        }
        if (lunge.phase == ZombieLunge::Phase::striking) {
            const float duration = std::min(remaining, std::max(0.f, zombieLungeSeconds - lunge.elapsed));
            if (!lunge.strikeStopped) {
                const float distance = std::min(std::max(0.f, zombieLungeDistance - lunge.forwardTravel),
                    zombieLungeSpeed * duration);
                const auto movement = move(feet, feet + lunge.direction * distance, zombieLungeSpeed,
                    world, crowd, zombie, players, playerCount);
                lunge.forwardTravel += movement.travel;
                commit(instance, movement.feet, duration);
                if (movement.contact && canZombieAttack(instance, *movement.contact, world, hitRays))
                    hits.push_back({zombie, movement.contact->id,
                        zombieAttackDamage(movement.impactVelocity, movement.contact->velocity)});
                lunge.strikeStopped = movement.contact || movement.blocked ||
                    lunge.forwardTravel + tolerance >= zombieLungeDistance;
            } else commit(instance, feet, duration);
            lunge.elapsed += duration;
            remaining -= duration;
            if (lunge.elapsed + 1e-6f < zombieLungeSeconds) return;
            recover(lunge, instance);
            continue;
        }
        if (lunge.phase == ZombieLunge::Phase::ready) return;
        const float duration = std::min(remaining, std::max(0.f, zombieAttackRecoverySeconds - lunge.elapsed));
        const float distance = horizontalDistance(feet, lunge.origin);
        auto desired = feet;
        if (distance > tolerance) desired += (lunge.origin - feet) *
            (std::min(distance, lunge.returnSpeed * duration) / distance);
        const auto movement = move(feet, desired, lunge.returnSpeed, world, crowd, zombie, players, playerCount);
        commit(instance, movement.feet, duration);
        lunge.elapsed += duration;
        if (movement.contact || movement.blocked || lunge.elapsed + 1e-6f >= zombieAttackRecoverySeconds)
            cancelZombieLunge(lunge, now);
        return;
    }
}
} // namespace viewer
