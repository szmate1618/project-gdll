#include "vehicle_player.hpp"
#include "zombie_layer.hpp"
#include <algorithm>
#include <cmath>

namespace viewer {
void collideVehicleZombies(VehicleLayer& cars, std::size_t car, const BoxPose& previous,
                          ZombieLayer& zombies, const CollisionWorld& world,
                          float seconds, double animationSeconds) {
    if (seconds <= 0) return;
    const auto current = cars.pose(car).chassis;
    const auto travel = current.center - previous.center;
    const float distance = glm::length(travel);
    // Broad phase independent of camera/behavior activation. The radius covers
    // both poses, wheel-width padding and the full height of an upright zombie.
    std::vector<std::size_t> candidates;
    zombies.queryLivingNear((previous.center + current.center) * .5f, 4.f + distance * .5f, candidates);
    const float turn = 2 * std::acos(std::clamp(std::abs(glm::dot(previous.rotation, current.rotation)), 0.f, 1.f));
    const int steps = std::clamp(static_cast<int>(std::ceil((distance + turn * 2) / .1f)), 1, 64);
    for (const auto id : candidates) {
        const auto feet = glm::vec3(zombies.instances[id].restRoot[3]);
        for (int sample = 0; sample <= steps; ++sample) {
            const float t = static_cast<float>(sample) / steps;
            auto box = current;
            box.center = glm::mix(previous.center, current.center, t);
            box.rotation = glm::slerp(previous.rotation, current.rotation, t);
            const auto contacts = boxContacts(box, feet, ZombieCrowd::bodyRadius, ZombieCrowd::bodyHeight, .025f);
            if (contacts.empty()) continue;
            const auto& hit = *std::max_element(contacts.begin(), contacts.end(), [](const Contact& a, const Contact& b) { return a.depth < b.depth; });
            auto normal = glm::vec3(hit.normal.x, 0, hit.normal.z);
            const float length = glm::length(normal);
            if (length < .1f) continue;
            normal /= length;
            const auto bodyCenter = feet + glm::vec3(0, .9f, 0);
            if (!world.lineOfSight(box.center, bodyCenter)) break;
            const auto velocity = cars.pointVelocity(car, hit.point);
            // Displacement also captures the pre-impact motion when a wall
            // stopped the rigid body during this very physics step.
            const float approach = std::max(glm::dot(velocity, normal), glm::dot(travel / seconds, normal));
            if (approach >= 5.f) {
                const auto launch = normal * (approach * .8f) + glm::vec3(0, .8f, 0);
                if (zombies.knockDown(id, launch, animationSeconds))
                    cars.applyImpulse(car, -normal * (75.f * approach * .8f), hit.point);
            } else {
                const auto overlap = boxContacts(current, feet, ZombieCrowd::bodyRadius, ZombieCrowd::bodyHeight, .025f);
                float depth = hit.depth;
                for (const auto& c : overlap) depth = std::max(depth, c.depth);
                const float push = std::min(.4f, depth + std::max(approach, 0.f) * seconds + .015f);
                const auto moved = zombies.pushLiving(id, normal * push, world);
                const float resistance = glm::length(moved) < push * .5f ? 3500.f * seconds : 75.f * std::max(approach, 0.f);
                if (approach > 0) cars.applyImpulse(car, -normal * resistance, hit.point);
            }
            break; // At most one reaction per zombie per car per fixed step.
        }
    }
}
} // namespace viewer
