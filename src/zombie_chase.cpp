#include "zombie_chase.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr double tau = 6.283185307179586;
}

bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds) {
    const float yaw = std::atan2(instance.restRoot[2].x, instance.restRoot[2].z);
    const float targetYaw = std::atan2(direction.x, direction.y);
    const float delta = std::remainder(targetYaw - yaw, static_cast<float>(tau));
    const float maxTurn = static_cast<float>(tau / 4) * seconds;
    const bool aligned = std::abs(delta) <= maxTurn + 1e-5f;
    const float nextYaw = aligned ? targetYaw : yaw + std::clamp(delta, -maxTurn, maxTurn);
    const auto feet = glm::vec3(instance.restRoot[3]);
    instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), nextYaw, {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    instance.walking = false;
    return aligned;
}

bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& /*asset*/,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double /*animationSeconds*/,
                      ZombieCrowd* crowd, std::size_t id) {
    auto feet = glm::vec3(instance.restRoot[3]);
    const auto start = feet;
    const glm::vec2 horizontal(target.x - feet.x, target.z - feet.z);
    const float distance = glm::length(horizontal);
    instance.walking = distance > 1e-4f;
    if (!instance.walking) return true;
    const auto direction = horizontal / distance;
    const float step = std::min(speed * seconds, distance);
    feet.x += direction.x * step;
    feet.z += direction.y * step;
    // Terrain-only support follows the rendered triangle surface, below roofs.
    const auto support = [&](glm::vec3& position) {
        const auto ground = world.groundAt(position.x, position.z,
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
            world.stats().terrainTriangles != 0);
        if (ground) position.y = ground->height;
    };
    support(feet);
    if (crowd) {
        const auto accepted = crowd->constrain(id, feet);
        if (accepted.x != feet.x || accepted.z != feet.z) {
            feet = accepted;
            if (feet.x == start.x && feet.z == start.z) feet.y = start.y;
            else support(feet);
        }
    }

    const glm::vec2 travelled(feet.x - start.x, feet.z - start.z);
    const float travel = glm::length(travelled);
    instance.walking = travel > 1e-5f;
    const auto facing = instance.walking ? travelled / travel : direction;
    const float yaw = std::atan2(facing.x, facing.y);
    instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), yaw, {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    instance.running = speed > 1.2f;
    // Advance the selected authored clip by actual travel time, excluding any
    // leftover step after arrival. No per-character skeleton is evaluated here.
    if (speed > 0 && instance.walking) instance.walkSeconds += travel / speed;
    return glm::length(glm::vec2(target.x - feet.x, target.z - feet.z)) <= 1e-4f;
}

} // namespace viewer
